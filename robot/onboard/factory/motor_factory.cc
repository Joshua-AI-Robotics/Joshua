#include "robot/onboard/factory/motor_factory.h"
#include "robot/onboard/drivers/sts3215_driver.h"
#include "robot/comm_interface/serial/serial.h"

MotorFactory::MotorFactory() {
    // Constructor implementation (empty for now)
}

std::unique_ptr<MotorInterface> MotorFactory::CreateMotor(
    MotorType type,
    const std::shared_ptr<Serial>& serial,
    int id
)
{
    switch (type)
    {
        case MotorType::STS3215:
        {
            return std::make_unique<Sts3215Driver>(serial, id);
        }
        default:
            return nullptr;
    }
}