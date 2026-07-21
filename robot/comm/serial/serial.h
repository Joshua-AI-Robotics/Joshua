#pragma once

#include <glog/logging.h>

#include <boost/asio.hpp>
#include <boost/asio/serial_port_base.hpp>
#include <mutex>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::comm {

// Byte-level transport boundary that bus-protocol boards (e.g.
// FeetechBusBoard) depend on, so their protocol logic is testable without a
// real serial port (docs/BOARD_LAYER_RFC.md §5.6). Serial is the only
// production implementation; fakes implement this directly.
class SerialTransport {
 public:
  virtual ~SerialTransport() = default;
  virtual absl::Status Write(const std::vector<uint8_t>& data) = 0;
  // Atomic Write-then-Read operation to prevent bus collisions.
  virtual absl::StatusOr<std::vector<uint8_t>> AtomicRead(const std::vector<uint8_t>& command,
                                                           size_t expected_response_size) = 0;
};

class Serial : public SerialTransport {
 public:
  Serial(std::shared_ptr<boost::asio::io_context> io, std::string uart_port, int uart_baudrate);
  ~Serial();
  absl::Status Write(const std::vector<uint8_t>& data) override;
  absl::StatusOr<std::vector<uint8_t>> Read(size_t bytes_to_read);

  absl::StatusOr<std::vector<uint8_t>> AtomicRead(const std::vector<uint8_t>& command,
                                                  size_t expected_response_size) override;

  absl::Status Flush();
  absl::Status Open();

 private:
  std::string uart_port_;
  int uart_baudrate_;
  std::shared_ptr<boost::asio::io_context> io_context_;
  std::unique_ptr<boost::asio::serial_port> serial_;
  std::mutex mutex_;  // UART Bus can use single serial. To avoid race condition.
};
}  // namespace robot::comm
