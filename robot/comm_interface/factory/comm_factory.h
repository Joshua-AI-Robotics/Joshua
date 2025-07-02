#pragma once

#include "robot/comm_interface/serial/serial.h"
#include "robot/config/robot.pb.h"
#include <boost/asio.hpp>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <utility> // For std::pair

namespace robot::comm_interface {

class CommFactory {
public:
    // Singleton access method
    static CommFactory& GetInstance() {
        static CommFactory instance;
        return instance;
    }

    // Delete copy constructor and assignment operator for singleton
    CommFactory(const CommFactory&) = delete;
    void operator=(const CommFactory&) = delete;

    std::shared_ptr<Serial> GetSerial(const robot::comm_interface::SerialConfig& config);

private:
    CommFactory();
    ~CommFactory();

    // One io_context for all serial communication.
    std::shared_ptr<boost::asio::io_context> io_context_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_;
    std::thread io_context_thread_;
    std::map<std::pair<std::string, uint32_t>, std::shared_ptr<Serial>> serials_;
};

} // namespace robot::comm_interface 