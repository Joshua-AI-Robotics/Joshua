#pragma once

#include "robot/onboard/interfaces/motor_interface.h"
#include "robot/comm_interface/serial/serial.h"

#include <memory>
#include <string>

// Forward declare Boost.Asio io_context
namespace boost::asio {
    class io_context;
}

enum class MotorType {
    STS3215,
    // Add other motor types here
};

class MotorFactory {
public:
    MotorFactory();
    ~MotorFactory() = default;

    // TODO: Make this input arguments as a class.
    static std::unique_ptr<MotorInterface> CreateMotor(
        MotorType type,
        const std::shared_ptr<Serial>& serial,
        int id
    );
};