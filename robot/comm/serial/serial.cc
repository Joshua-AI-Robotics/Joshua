#include "robot/comm/serial/serial.h"

#include <termios.h>

#include <cerrno>
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
    throw std::runtime_error("Error opening serial port " + uart_port_ + ": " + e.what());
  }
}

Serial::~Serial() {
  if (serial_ && serial_->is_open()) {
    boost::system::error_code error;
    serial_->close(error);
    if (error) LOG(ERROR) << "Error closing serial port " << uart_port_ << ": " << error.message();
  }
}

absl::Status Serial::Open() {
  if (!serial_->is_open()) {
    return absl::Status(absl::StatusCode::kInternal, "Serial port not open.");
  }
  return absl::OkStatus();
}

absl::Status Serial::Write(const std::vector<uint8_t>& data) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!serial_->is_open()) {
    return absl::Status(absl::StatusCode::kInternal, "Serial port not open for writing.");
  }
  try {
    boost::asio::write(*serial_, boost::asio::buffer(data));
  } catch (const boost::system::system_error& e) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Error writing to serial port " + uart_port_ + ": " + e.what());
  }
  return absl::OkStatus();
}

absl::StatusOr<std::vector<uint8_t>> Serial::Read(size_t bytes_to_read) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!serial_->is_open()) {
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
    timer.cancel();
    return absl::Status(absl::StatusCode::kInternal,
                        "Error reading from serial port " + uart_port_ + ": " + e.what());
  }

  timer.cancel();  // Cancel the timer if read completes

  if (ec == boost::asio::error::operation_aborted) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Serial read operation timed out or was cancelled.");
  } else if (ec) {
    return absl::Status(absl::StatusCode::kInternal,
                        "Read failed on " + uart_port_ + ": " + ec.message());
  }

  if (bytes_read != bytes_to_read) {
    return absl::DataLossError("Short read on " + uart_port_ + ": received " +
                               std::to_string(bytes_read) + ", expected " +
                               std::to_string(bytes_to_read));
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
    return absl::InternalError("tcflush failed on " + uart_port_ + ": " + strerror(errno));
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
    timer.cancel();
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
  std::lock_guard<std::mutex> lock(mutex_);
  if (!serial_->is_open()) {
    return absl::Status(absl::StatusCode::kInternal, "Serial port not open for flushing.");
  }
  // Using tcflush for POSIX systems is more reliable for clearing serial buffers.
  if (::tcflush(serial_->native_handle(), TCIFLUSH) != 0) {
    return absl::InternalError("tcflush failed on " + uart_port_ + ": " + strerror(errno));
  }
  return absl::OkStatus();
}

}  // namespace robot::comm
