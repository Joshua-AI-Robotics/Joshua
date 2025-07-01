#include "robot/comm_interface/factory/comm_factory.h"
#include <boost/asio.hpp>

namespace robot::comm_interface {

CommFactory::CommFactory() : io_context_(std::make_shared<boost::asio::io_context>()) {}

CommFactory::~CommFactory() {
    if (io_context_thread_.joinable()) {
        work_guard_.reset(); // Allow io_context::run() to exit.
        io_context_->stop();
        io_context_thread_.join();
    }
}

std::shared_ptr<Serial> CommFactory::GetSerial(const robot::comm_interface::SerialConfig& config) {
    auto key = std::make_pair(config.port(), config.baudrate());
    auto it = serials_.find(key);

    if (it != serials_.end()) {
        return it->second;
    }

    // If we are creating the first serial port, start the io_context.
    if (serials_.empty()) {
        // The work guard is necessary to keep io_context::run() from returning
        // when it has no more work to do.
        work_guard_ = std::make_unique<boost::asio::io_context::work>(*io_context_);
        io_context_thread_ = std::thread([this]() { io_context_->run(); });
    }

    auto new_serial = std::make_shared<Serial>(io_context_, config.port(), config.baudrate());
    serials_.emplace(key, new_serial);
    return new_serial;
}

} // namespace robot::comm_interface 