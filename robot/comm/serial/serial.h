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
#include "absl/types/span.h"
#include "robot/comm/interfaces/byte_stream.h"

namespace robot::comm {

// A serial mechanism can provide both an ordered byte stream and atomic
// byte transactions. Message framing belongs to FramedSerialTransport.
class Serial : public ByteStream {
 public:
  Serial(std::shared_ptr<boost::asio::io_context> io,
         std::string uart_port,
         int uart_baudrate,
         std::chrono::milliseconds post_open_settle = std::chrono::milliseconds(0));
  ~Serial();
  absl::Status Write(const std::vector<uint8_t>& data) override;
  absl::StatusOr<std::vector<uint8_t>> Read(size_t bytes_to_read) override;

  absl::StatusOr<std::vector<uint8_t>> AtomicRead(const std::vector<uint8_t>& command,
                                                  size_t expected_response_size);
  // Compatibility for the existing comm-local v1 diagnostic.
  absl::StatusOr<std::vector<uint8_t>> SendAndReceive(const std::vector<uint8_t>& request,
                                                      size_t expected_response_size) {
    return AtomicRead(request, expected_response_size);
  }

  absl::Status Flush();
  absl::Status Open() override;
  // Mechanism-only transaction. The adapter consumes each received byte and
  // returns true at its message boundary, or an error. Runs under the bus lock;
  // callbacks must not re-enter Serial. No protocol bytes or lengths live here.
  absl::Status ExchangeUntil(absl::Span<const uint8_t> request,
                             std::chrono::milliseconds timeout,
                             const std::function<absl::StatusOr<bool>(uint8_t)>& consume);

 private:
  std::string uart_port_;
  int uart_baudrate_;
  std::shared_ptr<boost::asio::io_context> io_context_;
  std::unique_ptr<boost::asio::serial_port> serial_;
  std::timed_mutex mutex_;  // One bus lock across byte, legacy and framed I/O.
};
}  // namespace robot::comm
