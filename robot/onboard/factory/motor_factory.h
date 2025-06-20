#pragma once

#include "robot/onboard/interfaces/motor_interface.h"
#include "robot/comm_interface/serial/serial.h"

#include <memory>
#include <string>

// Forward declare Boost.Asio io_context
namespace boost::asio {
    class io_context;
}

namespace robot::onboard{
enum class MotorType {
    STS3215,
    // Add other motor types here
};

class MotorFactory {
public:
    MotorFactory();
    ~MotorFactory() = default;

    // TODO: Make this input arguments as a class.
    static std::unique_ptr<robot::onboard::MotorInterface> CreateMotor(
        MotorType type,
        const std::shared_ptr<robot::comm_interface::Serial>& serial,
        int id
    );
};
}