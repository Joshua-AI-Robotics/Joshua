#pragma once

#include "robot/onboard/interfaces/motor_interface.h"
#include "robot/comm_interface/serial/serial.h"
#include "robot/onboard/drivers/sts3215_driver.h"

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
    MotorFactory() = default;
    ~MotorFactory() = default;

    template <class T>
    static std::unique_ptr<robot::onboard::MotorInterface> CreateMotor(
        MotorType type,
        const std::shared_ptr<T>& comm, // Find a better name?
        int id
    );
};

// Template method definitions must be in the header file
// so that the compiler can see the full definition when it instantiates the template.
template <class T>
std::unique_ptr<robot::onboard::MotorInterface> MotorFactory::CreateMotor(
    MotorType type,
    const std::shared_ptr<T>& comm,
    int id
)
{
    switch (type)
    {
        case MotorType::STS3215:
        {
            // Cast the generic communication interface to a Serial interface for STS3215
            auto serial_comm = std::static_pointer_cast<robot::comm_interface::Serial>(comm);
            return std::make_unique<robot::onboard::Sts3215Driver>(serial_comm, static_cast<uint8_t>(id));
        }
        default:
            return nullptr;
    }
}
}