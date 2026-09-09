#pragma once

#include <glog/logging.h>

#include <boost/asio.hpp>
#include <boost/asio/serial_port_base.hpp>
#include <mutex>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/interfaces/stream_transport.h"

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

// Implements both byte-level seams: SerialTransport for bus-protocol boards
// that need the atomic write-then-read (FeetechBusBoard), and
// StreamTransport for single-stream sensor drivers that just pull bytes
// (Lds01Driver). Neither of those depends on this class.
class Serial : public SerialTransport, public StreamTransport {
 public:
  Serial(std::shared_ptr<boost::asio::io_context> io, std::string uart_port, int uart_baudrate);
  ~Serial();
  absl::Status Write(const std::vector<uint8_t>& data) override;
  absl::StatusOr<std::vector<uint8_t>> Read(size_t bytes_to_read) override;

  absl::StatusOr<std::vector<uint8_t>> AtomicRead(const std::vector<uint8_t>& command,
                                                  size_t expected_response_size) override;

  absl::Status Flush();
  absl::Status Open() override;

 private:
  std::string uart_port_;
  int uart_baudrate_;
  std::shared_ptr<boost::asio::io_context> io_context_;
  std::unique_ptr<boost::asio::serial_port> serial_;
  std::mutex mutex_;  // UART Bus can use single serial. To avoid race condition.
};
}  // namespace robot::comm
