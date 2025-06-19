#include "robot/onboard/factory/motor_factory.h"
#include "robot/onboard/drivers/sts3215_driver.h"
#include "robot/comm_interface/serial/serial.h"

MotorFactory::MotorFactory() {
    // Constructor implementation (empty for now)
}

std::unique_ptr<MotorInterface> MotorFactory::CreateMotor(
    MotorType type,
    boost::asio::io_context& io_context,
    const std::string& port_name,
    unsigned int baudrate,
    int motor_id
)
{
    switch (type)
    {
        case MotorType::STS3215:
        {
            auto serial = std::make_unique<Serial>(io_context, port_name, baudrate);
            return std::make_unique<Sts3215Driver>(std::move(serial));
        }
        default:
            return nullptr;
    }
}