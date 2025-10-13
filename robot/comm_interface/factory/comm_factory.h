#pragma once

#include "robot/comm_interface/serial/serial.h"
#include "config/proto/robot.pb.h"
#include <boost/asio.hpp>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <mutex>
#include <utility> 

namespace robot::comm_interface {

class CommFactory {
public:
    static CommFactory& GetInstance() {
        static CommFactory instance;
        return instance;
    }

    CommFactory(const CommFactory&) = delete;
    void operator=(const CommFactory&) = delete;

    std::shared_ptr<Serial> GetSerial(const robot::comm_interface::SerialConfig& config);
    // TODO: Remove GetSerial and use 'CreateComm'.

private:
    CommFactory() = default;
    ~CommFactory();

    struct PortResources {
        std::shared_ptr<boost::asio::io_context> io_context;
        boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard;
        std::thread io_context_thread;
        std::map<uint32_t, std::shared_ptr<Serial>> serials;

        PortResources()
            : io_context(std::make_shared<boost::asio::io_context>()),
              work_guard(boost::asio::make_work_guard(*io_context)),
              io_context_thread([this] { io_context->run(); }) {}

        ~PortResources() {
            work_guard.reset();
            if (io_context_thread.joinable()) {
                io_context_thread.join();
            }
        }
    };

    std::map<std::string, std::unique_ptr<PortResources>> port_resources_;
    std::mutex mutex_;
};

} 