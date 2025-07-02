#include "robot/comm_interface/factory/comm_factory.h"
#include <boost/asio.hpp>

namespace robot::comm_interface {

CommFactory::CommFactory() :
    io_context_(std::make_shared<boost::asio::io_context>()),
    work_guard_(boost::asio::make_work_guard(io_context_->get_executor()))
{
    io_context_thread_ = std::thread([this]() { io_context_->run(); });
}

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

    auto new_serial = std::make_shared<Serial>(io_context_, config.port(), config.baudrate());
    serials_.emplace(key, new_serial);
    return new_serial;
}

} // namespace robot::comm_interface 