#include "robot/comm/serial/serial.h"

#include <fcntl.h>
#include <poll.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <thread>

namespace robot::comm {
Serial::Serial(std::shared_ptr<boost::asio::io_context> io,
               std::string uart_port,
               int uart_baudrate,
               std::chrono::milliseconds post_open_settle)
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
    std::this_thread::sleep_for(post_open_settle);
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
  std::lock_guard<std::timed_mutex> lock(mutex_);
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
  std::lock_guard<std::timed_mutex> lock(mutex_);
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
  std::lock_guard<std::timed_mutex> lock(mutex_);

  if (!serial_->is_open()) {
    return absl::Status(absl::StatusCode::kInternal, "Serial port not open for query.");
  }

  // 1. Flush (Clear input buffer before sending command)
  if (::tcflush(serial_->native_handle(), TCIFLUSH) != 0) {
    LOG(WARNING) << "tcflush failed during query: " << strerror(errno);
  }

  // 2. Write Command
  try {
    boost::asio::write(*serial_, boost::asio::buffer(command));
  } catch (const boost::system::system_error& e) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Error writing query command: " + std::string(e.what()));
  }

  // 3. Read Response
  std::vector<uint8_t> buffer(expected_response_size);
  boost::system::error_code ec;

  boost::asio::steady_timer timer(*io_context_);
  timer.expires_after(std::chrono::milliseconds(20));  // Slightly longer timeout for full query
  timer.async_wait([&](const boost::system::error_code& e) {
    if (!e) serial_->cancel();
  });

  try {
    boost::asio::read(*serial_, boost::asio::buffer(buffer), ec);
  } catch (const boost::system::system_error& e) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Error reading query response: " + std::string(e.what()));
  }

  timer.cancel();

  if (ec) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Query read failed/timed out: " + ec.message());
  }

  return buffer;
}

absl::Status Serial::Flush() {
  std::lock_guard<std::timed_mutex> lock(mutex_);
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

absl::Status Serial::ExchangeUntil(absl::Span<const uint8_t> request,
                                   std::chrono::milliseconds timeout,
                                   const std::function<absl::StatusOr<bool>(uint8_t)>& consume) {
  if (request.empty() || timeout.count() <= 0 || timeout.count() > INT_MAX || !consume)
    return absl::InvalidArgumentError("Invalid serial transaction or deadline.");
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::unique_lock<std::timed_mutex> lock(mutex_, std::defer_lock);
  if (!lock.try_lock_until(deadline))
    return absl::DeadlineExceededError("Serial bus busy; request not sent.");
  if (!serial_->is_open()) return absl::FailedPreconditionError("Serial port is closed.");
  const int fd = serial_->native_handle();
  const int flags = fcntl(fd, F_GETFL);
  if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    return absl::UnavailableError("Cannot configure serial exchange deadline.");
  }
  struct RestoreFlags {
    int fd;
    int flags;
    ~RestoreFlags() {
      (void)fcntl(fd, F_SETFL, flags);
    }
  } restore{fd, flags};
  if (tcflush(fd, TCIFLUSH) != 0) {
    return absl::UnavailableError("Cannot flush serial input before exchange.");
  }
  auto wait = [&](short events) -> absl::Status {
    for (;;) {
      const auto remaining = deadline - std::chrono::steady_clock::now();
      if (remaining <= std::chrono::steady_clock::duration::zero()) {
        return absl::DeadlineExceededError("Serial framed exchange timed out; outcome unknown.");
      }
      pollfd descriptor{fd, events, 0};
      const int poll_timeout =
          static_cast<int>(
              std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count()) +
          1;
      const int result = poll(&descriptor, 1, poll_timeout);
      if (result < 0 && errno == EINTR) continue;
      if (result < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        return absl::UnavailableError(
            "Serial framed exchange lost its connection; outcome unknown.");
      }
      if (result > 0 && (descriptor.revents & events) &&
          std::chrono::steady_clock::now() < deadline)
        return absl::OkStatus();
    }
  };
  size_t written = 0;
  while (written < request.size()) {
    auto status = wait(POLLOUT);
    if (!status.ok()) return status;
    const ssize_t count = write(fd, request.data() + written, request.size() - written);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (count <= 0) return absl::UnavailableError("Serial framed write failed; outcome unknown.");
    written += static_cast<size_t>(count);
  }
  for (;;) {
    auto status = wait(POLLIN);
    if (!status.ok()) return status;
    uint8_t byte;
    const ssize_t count = read(fd, &byte, 1);
    if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
    if (count <= 0) return absl::UnavailableError("Serial framed read failed; outcome unknown.");
    auto complete = consume(byte);
    if (!complete.ok()) return complete.status();
    if (*complete) return absl::OkStatus();
  }
}

}  // namespace robot::comm
