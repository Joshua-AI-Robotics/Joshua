#pragma once

#include <glog/logging.h>

#include <boost/asio.hpp>
#include <boost/asio/serial_port_base.hpp>
#include <chrono>
#include <functional>
#include <mutex>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "robot/comm/interfaces/byte_stream.h"
#include "robot/comm/interfaces/message_framer.h"

namespace robot::comm {

// One open serial port. Provides an ordered byte stream plus deadline-bounded
// send and framed request/response operations; every operation holds the port
// for its whole duration so shared half-duplex buses never interleave.
class Serial : public ByteStream {
 public:
  Serial(std::shared_ptr<boost::asio::io_context> io, std::string uart_port, int uart_baudrate);
  ~Serial();
  absl::Status Write(const std::vector<uint8_t>& data) override;
  absl::StatusOr<std::vector<uint8_t>> Read(size_t bytes_to_read) override;

  // Atomic write-then-read of a fixed-size response, for bring-up tools.
  absl::StatusOr<std::vector<uint8_t>> AtomicRead(const std::vector<uint8_t>& command,
                                                  size_t expected_response_size);

  // Writes `data` within `timeout`.
  absl::Status Send(absl::Span<const uint8_t> data, absl::Duration timeout);

  // Discards pending input, writes `request`, then reads one response whose
  // end `framer` determines, all within `timeout`.
  absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request,
                                                const MessageFramer& framer,
                                                absl::Duration timeout);

  absl::Status Flush();
  absl::Status Open() override;

 private:
  using AsyncCompletion = std::function<void(const boost::system::error_code&, std::size_t)>;

  // Starts one asynchronous operation on the io_context thread and waits for
  // it until `deadline`, cancelling it if the deadline passes first. Callers
  // hold mutex_.
  absl::Status AwaitOperation(const std::function<void(AsyncCompletion)>& start,
                              std::chrono::steady_clock::time_point deadline,
                              const char* operation);

  std::string uart_port_;
  int uart_baudrate_;
  std::shared_ptr<boost::asio::io_context> io_context_;
  std::unique_ptr<boost::asio::serial_port> serial_;
  std::mutex mutex_;  // UART Bus can use single serial. To avoid race condition.
};
}  // namespace robot::comm
