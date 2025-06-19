#pragma once

#include <boost/asio.hpp>
#include <boost/asio/serial_port_base.hpp>
#include <glog/logging.h>
#include <mutex>

class Serial {
  public:
    Serial(boost::asio::io_context& io, std::string uart_port, int uart_baudrate);
    ~Serial();
    void Write(const std::vector<uint8_t>& data);
    std::string Read();

  private:
    std::string uart_port_;
    int uart_baudrate_;
    boost::asio::io_context& io_context_;
    std::unique_ptr<boost::asio::serial_port> serial_;
    std::mutex mutex_;
};