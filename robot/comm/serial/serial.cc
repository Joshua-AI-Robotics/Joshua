#include "robot/comm/serial/serial.h"

#include <termios.h>

#include <cerrno>
#include <cstring>
#include <future>

#include "absl/strings/str_cat.h"
#include "utils/status_macros.h"

namespace robot::comm {
namespace {

// Bounds a single framed response so a misbehaving framer cannot grow it
// without limit.
constexpr size_t kMaxFramedResponseBytes = 4096;

absl::StatusOr<std::chrono::steady_clock::time_point> DeadlineAfter(absl::Duration timeout) {
  if (timeout <= absl::ZeroDuration() || timeout == absl::InfiniteDuration()) {
    return absl::InvalidArgumentError("Serial timeout must be positive and finite.");
  }
  return std::chrono::steady_clock::now() +
         std::chrono::duration_cast<std::chrono::steady_clock::duration>(
             absl::ToChronoNanoseconds(timeout));
}

}  // namespace

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

absl::Status Serial::Send(absl::Span<const uint8_t> data, absl::Duration timeout) {
  ABSL_ASSIGN_OR_RETURN(const auto deadline, DeadlineAfter(timeout));
  std::lock_guard<std::mutex> lock(mutex_);
  if (!serial_->is_open()) {
    return absl::InternalError("Serial port not open for writing.");
  }
  return AwaitOperation(
      [this, data](AsyncCompletion done) {
        boost::asio::async_write(
            *serial_, boost::asio::buffer(data.data(), data.size()), std::move(done));
      },
      deadline,
      "write");
}

absl::StatusOr<std::vector<uint8_t>> Serial::Exchange(absl::Span<const uint8_t> request,
                                                      const MessageFramer& framer,
                                                      absl::Duration timeout) {
  ABSL_ASSIGN_OR_RETURN(const auto deadline, DeadlineAfter(timeout));
  std::lock_guard<std::mutex> lock(mutex_);
  if (!serial_->is_open()) {
    return absl::InternalError("Serial port not open for query.");
  }

  if (::tcflush(serial_->native_handle(), TCIFLUSH) != 0) {
    LOG(WARNING) << "tcflush failed during query: " << strerror(errno);
  }

  ABSL_RETURN_IF_ERROR(AwaitOperation(
      [this, request](AsyncCompletion done) {
        boost::asio::async_write(
            *serial_, boost::asio::buffer(request.data(), request.size()), std::move(done));
      },
      deadline,
      "write"));

  std::vector<uint8_t> response;
  while (true) {
    ABSL_ASSIGN_OR_RETURN(const size_t remaining, framer.RemainingBytes(response));
    if (remaining == 0) {
      return response;
    }
    if (remaining > kMaxFramedResponseBytes - response.size()) {
      return absl::InternalError(absl::StrCat(
          "Framed response on ", uart_port_, " exceeds ", kMaxFramedResponseBytes, " bytes."));
    }
    const size_t received = response.size();
    response.resize(received + remaining);
    ABSL_RETURN_IF_ERROR(AwaitOperation(
        [this, &response, received, remaining](AsyncCompletion done) {
          boost::asio::async_read(*serial_,
                                  boost::asio::buffer(response.data() + received, remaining),
                                  std::move(done));
        },
        deadline,
        "read"));
  }
}

absl::Status Serial::AwaitOperation(const std::function<void(AsyncCompletion)>& start,
                                    std::chrono::steady_clock::time_point deadline,
                                    const char* operation) {
  if (io_context_->stopped()) {
    return absl::UnavailableError(
        absl::StrCat("Serial ", operation, " on ", uart_port_, ": I/O context is stopped."));
  }
  auto result = std::make_shared<std::promise<boost::system::error_code>>();
  std::future<boost::system::error_code> completed = result->get_future();
  boost::asio::post(*io_context_, [&start, result] {
    start([result](const boost::system::error_code& ec, std::size_t) { result->set_value(ec); });
  });

  bool timed_out = false;
  if (completed.wait_until(deadline) == std::future_status::timeout) {
    timed_out = true;
    // Cancel on the io_context thread, which owns the pending operation, and
    // wait for the cancel to run so it cannot reach a later operation.
    std::promise<void> cancelled;
    std::future<void> cancel_done = cancelled.get_future();
    boost::asio::post(*io_context_, [this, &cancelled] {
      boost::system::error_code ignored;
      serial_->cancel(ignored);
      cancelled.set_value();
    });
    cancel_done.wait();
  }

  const boost::system::error_code ec = completed.get();
  if (!ec) {
    return absl::OkStatus();
  }
  if (timed_out && ec == boost::asio::error::operation_aborted) {
    return absl::DeadlineExceededError(
        absl::StrCat("Serial ", operation, " on ", uart_port_, " timed out."));
  }
  return absl::UnavailableError(
      absl::StrCat("Serial ", operation, " on ", uart_port_, " failed: ", ec.message()));
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
