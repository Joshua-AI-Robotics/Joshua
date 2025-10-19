#pragma once

#include <boost/asio.hpp>
#include <boost/asio/serial_port_base.hpp>
#include <glog/logging.h>
#include <mutex>
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::comm_interface{

class Serial {
  public:
    Serial(std::shared_ptr<boost::asio::io_context> io, std::string uart_port, int uart_baudrate);
    ~Serial();
    absl::Status Write(const std::vector<uint8_t>& data);
    absl::StatusOr<std::vector<uint8_t>> Read(size_t bytes_to_read);
    absl::Status Flush();
    absl::Status Open();

  private:
    std::string uart_port_;
    int uart_baudrate_;
    std::shared_ptr<boost::asio::io_context> io_context_;
    std::unique_ptr<boost::asio::serial_port> serial_;
    std::mutex mutex_; // UART Bus can use single serial. To avoid race condition.
};
}
