#include "robot/onboard/factory/motor_factory.h"
#include "robot/onboard/drivers/sts3215_driver.h"
#include "robot/comm_interface/serial/serial.h"

namespace robot::onboard {

MotorFactory::MotorFactory() {
    // Constructor implementation (empty for now)
}

std::unique_ptr<robot::onboard::MotorInterface> MotorFactory::CreateMotor(
    MotorType type,
    const std::shared_ptr<robot::comm_interface::Serial>& serial,
    int id
)
{
    switch (type)
    {
        case MotorType::STS3215:
        {
            return std::make_unique<robot::onboard::Sts3215Driver>(serial, static_cast<uint8_t>(id));
        }
        default:
            return nullptr;
    }
}

} // namespace robot::onboard