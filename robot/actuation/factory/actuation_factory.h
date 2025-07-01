#pragma once

#include "robot/actuation/interfaces/actuation_interface.h"
#include "robot/comm_interface/serial/serial.h"
#include "robot/actuation/motors/drivers/sts3215_driver.h"
#include "robot/config/robot.pb.h"

#include <memory>
#include <string>

using namespace robot::comm_interface;

// Forward declare Boost.Asio io_context
namespace boost::asio {
    class io_context;
}

namespace robot::actuation{
class ActuationFactory {
public:
    ActuationFactory() = default;
    ~ActuationFactory() = default;

    std::unique_ptr<robot::actuation::ActuationInterface> CreateActuator(robot::actuation::Motor motor_config)
    {
        // TODO: Fix this nested switch case. Probably should make comm_factory.
        switch (motor_config.motor_type())
        {
            case robot::actuation::MotorType::STS3215:
            {
                switch (motor_config.comm_type()){
                    case robot::comm_interface::CommType::SERIAL:
                        if (io_context_ == nullptr) {
                            io_context_ = std::make_shared<boost::asio::io_context>();
                        }
                        
                        auto port = motor_config.serial_config().port();
                        auto it = serials_.find(motor_config.serial_config().port());

                        // If serial port already exist (e.g. daisy-chain with uart)
                        if(it != serials_.end()){
                            return std::make_unique<robot::actuation::Sts3215Driver>(it->second, motor_config);
                        }

                        serials_.emplace(port, 
                            std::make_shared<Serial>(
                                io_context_,
                                port,
                                motor_config.serial_config().baudrate()
                            ));

                        return std::make_unique<robot::actuation::Sts3215Driver>(serials_[port], motor_config);
                }
            }
            default:
                return nullptr;
        }
    }

private:
    // Need one io_context for every serial.
    std::shared_ptr<boost::asio::io_context> io_context_;
    std::map<std::string, std::shared_ptr<Serial>> serials_;
};
}
