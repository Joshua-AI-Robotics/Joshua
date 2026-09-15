#include "robot/comm/serial/serial.h"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>

namespace robot::comm {
Serial::Serial(std::shared_ptr<boost::asio::io_context> io,
               std::string uart_port,
               int uart_baudrate)
    : io_context_(io), uart_port_(uart_port), uart_baudrate_(uart_baudrate) {
  try {
    serial_ = std::make_unique<boost::asio::serial_port>(*io_context_, uart_port_);

    serial_->set_option(boost::asio::serial_port_base::baud_rate(uart_baudrate));
    serial_->set_option(boost::asio::serial_port_base::character_size(8));
    serial_->set_option(
        boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
    serial_->set_option(
        boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
    serial_->set_option(boost::asio::serial_port_base::flow_control(
        boost::asio::serial_port_base::flow_control::none));
  } catch (const boost::system::system_error& e) {
    LOG(ERROR) << e.what();
    throw std::runtime_error("Error opening serial port.");
  }
}

Serial::~Serial() {
  if (serial_->is_open()) {
    try {
      serial_->close();
    } catch (const boost::system::system_error& e) {
      LOG(ERROR) << "Error closing serial port: " << e.what();
      throw std::runtime_error("Error closing serial port.");
    }
  }
}

absl::Status Serial::Open() {
  if (!serial_->is_open()) {
    LOG(ERROR) << "Error: Serial port not open.";
    return absl::Status(absl::StatusCode::kInternal, "Serial port not open.");
  }
  return absl::OkStatus();
}

absl::Status Serial::Write(const std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!serial_->is_open()) {
    LOG(ERROR) << "Error: Serial port not open for writing.";
    return absl::Status(absl::StatusCode::kInternal, "Serial port not open for writing.");
  }
  try {
    boost::asio::write(*serial_, boost::asio::buffer(data));
  } catch (const boost::system::system_error& e) {
    LOG(ERROR) << "Error writing to serial port: " << e.what();
    return absl::Status(absl::StatusCode::kInternal, "Error writing to serial port.");
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<uint8_t>> Serial::Read(size_t bytes_to_read) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!serial_->is_open()) {
    LOG(ERROR) << "Error: Serial port not open for reading.";
    return absl::Status(absl::StatusCode::kInternal, "Serial port not open for reading.");
  }

  std::vector<uint8_t> buffer(bytes_to_read);
  boost::system::error_code ec;

  // Set up a deadline timer for the read operation
  boost::asio::steady_timer timer(*io_context_);
  timer.expires_after(std::chrono::milliseconds(10));  // 10ms timeout
  timer.async_wait([&](const boost::system::error_code& e) {
    if (!e) {             // Timer not cancelled, means timeout occurred
      serial_->cancel();  // Cancel the pending read operation
    }
  });

  size_t bytes_read = 0;
  try {
    bytes_read = boost::asio::read(*serial_, boost::asio::buffer(buffer), ec);
  } catch (const boost::system::system_error& e) {
    LOG(ERROR) << "Error reading from serial port: " << e.what();
    return absl::Status(absl::StatusCode::kInternal, "Error reading from serial port.");
  }

  timer.cancel();  // Cancel the timer if read completes

  if (ec == boost::asio::error::operation_aborted) {
    // LOG(ERROR) << "Serial read operation timed out or was cancelled.";
    return absl::Status(absl::StatusCode::kInternal,
                        "Serial read operation timed out or was cancelled.");
  } else if (ec) {
    LOG(ERROR) << "Error reading from serial port: " << ec.message();
    return absl::Status(absl::StatusCode::kInternal, "Read failed");
  }

  if (bytes_read != bytes_to_read) {
    LOG(WARNING) << "Read " << bytes_read << " bytes, expected " << bytes_to_read;
  }

  return buffer;
}

absl::StatusOr<std::vector<uint8_t>> Serial::AtomicRead(const std::vector<uint8_t>& command,
                                                        size_t expected_response_size) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (!serial_->is_open()) {
    return absl::Status(absl::StatusCode::kInternal, "Serial port not open for query.");
  }

  // 1. Flush (Clear input buffer before sending command)
  if (::tcflush(serial_->native_handle(), TCIFLUSH) != 0) {
    LOG(WARNING) << "tcflush failed during query: " << strerror(errno);
  }

  // A synchronous boost::asio::read is not cancelled by serial_port::cancel,
  // which only cancels asynchronous operations. Use nonblocking fd I/O and a
  // single deadline covering both write and read, including partial responses.
  const int fd = serial_->native_handle();
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return absl::InternalError("Cannot configure nonblocking serial query");
  }
  struct RestoreFlags {
    int fd, flags;
    ~RestoreFlags() {
      fcntl(fd, F_SETFL, flags);
    }
  } restore{fd, flags};
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(20);
  auto transfer = [&](uint8_t* data, size_t size, bool writing) -> absl::Status {
    size_t offset = 0;
    while (offset < size) {
      const auto remaining = deadline - std::chrono::steady_clock::now();
      if (remaining <= std::chrono::steady_clock::duration::zero()) {
        return absl::DeadlineExceededError("Serial query deadline exceeded");
      }
      const int wait_ms =
          static_cast<int>(
              std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()) +
          1;
      pollfd descriptor{fd, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
      const int ready = poll(&descriptor, 1, wait_ms);
      if (ready < 0 && errno == EINTR) continue;
      if (ready < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        return absl::UnavailableError("Serial query connection lost");
      }
      if (ready == 0) continue;
      const ssize_t count = writing ? ::write(fd, data + offset, size - offset)
                                    : ::read(fd, data + offset, size - offset);
      if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
      if (count <= 0) return absl::UnavailableError("Serial query transfer failed");
      offset += static_cast<size_t>(count);
    }
    return absl::OkStatus();
  };
  // transfer does not mutate outgoing bytes; a copy avoids casting away const.
  auto outgoing = command;
  auto status = transfer(outgoing.data(), outgoing.size(), true);
  if (!status.ok()) return status;
  std::vector<uint8_t> buffer(expected_response_size);
  status = transfer(buffer.data(), buffer.size(), false);
  if (!status.ok()) return status;

  return buffer;
}

absl::Status Serial::Flush() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!serial_->is_open()) {
    LOG(ERROR) << "Error: Serial port not open for flushing.";
    return absl::Status(absl::StatusCode::kInternal, "Serial port not open for flushing.");
  }
  // Using tcflush for POSIX systems is more reliable for clearing serial buffers.
  if (::tcflush(serial_->native_handle(), TCIFLUSH) != 0) {
    LOG(ERROR) << "tcflush failed: " << strerror(errno);
  }
  return absl::OkStatus();
}

}  // namespace robot::comm
