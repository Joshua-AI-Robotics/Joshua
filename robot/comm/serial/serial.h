#pragma once

#include <glog/logging.h>

#include <boost/asio.hpp>
#include <boost/asio/serial_port_base.hpp>
#include <mutex>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::comm {

class Serial {
 public:
  Serial(std::shared_ptr<boost::asio::io_context> io, std::string uart_port, int uart_baudrate);
  ~Serial();
  absl::Status Write(const std::vector<uint8_t>& data);
  absl::StatusOr<std::vector<uint8_t>> Read(size_t bytes_to_read);

  // Atomic Write-then-Read operation to prevent bus collisions
  absl::StatusOr<std::vector<uint8_t>> AtomicRead(const std::vector<uint8_t>& command,
                                                  size_t expected_response_size);

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
