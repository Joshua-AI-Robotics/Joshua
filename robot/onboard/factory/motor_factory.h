#pragma once

#include "robot/onboard/interfaces/motor_interface.h"
#include "robot/comm_interface/serial/serial.h"
#include "robot/onboard/drivers/sts3215_driver.h"
#include "robot/config/robot.pb.h"

#include <memory>
#include <string>

using namespace robot::comm_interface;

// Forward declare Boost.Asio io_context
namespace boost::asio {
    class io_context;
}

namespace robot::onboard{
class MotorFactory {
public:
    MotorFactory() = default;
    ~MotorFactory() = default;

    std::unique_ptr<robot::onboard::MotorInterface> CreateMotor(robot_config::Motor motor_config)
    {
        switch (motor_config.motor_type())
        {
            case robot_config::MotorType::STS3215:
            {
                switch (motor_config.comm_type()){
                    case robot_config::CommType::SERIAL:
                        if (io_context_ == nullptr) {
                            io_context_ = std::make_shared<boost::asio::io_context>();
                        }
                        if (serial_ == nullptr) {
                            serial_ = std::make_shared<Serial>(
                                io_context_,
                                motor_config.serial_config().port(),
                                motor_config.serial_config().baudrate()
                            );
                        }
                        return std::make_unique<robot::onboard::Sts3215Driver>(serial_, motor_config);
                }
            }
            default:
                return nullptr;
        }
    }

private:
    std::shared_ptr<Serial> serial_;
    std::shared_ptr<boost::asio::io_context> io_context_;
};
}
