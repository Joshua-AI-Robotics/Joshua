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
        boost::asio::io_context& io_context, // Pass the io_context from main/app
        const std::string& port_name,
        unsigned int baudrate,        
        int motor_id
    );
};