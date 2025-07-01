#pragma once

#include "robot/comm_interface/serial/serial.h"
#include "robot/config/robot.pb.h"
#include <memory>
#include <string>
#include <map>
#include <utility> // For std::pair

// Forward declare Boost.Asio io_context
namespace boost::asio {
    class io_context;
}

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

    // Need one io_context for every serial.
    std::shared_ptr<boost::asio::io_context> io_context_;
    std::map<std::pair<std::string, uint32_t>, std::shared_ptr<Serial>> serials_;
};

} // namespace robot::comm_interface 