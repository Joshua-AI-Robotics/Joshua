#pragma once

#include "robot/actuation/interfaces/actuation_interface.h"
#include "robot/comm_interface/factory/comm_factory.h"
#include "robot/actuation/motors/drivers/sts3215_driver.h"
#include "config/proto/robot.pb.h"

#include <memory>
#include <string>

namespace robot::actuation{
class ActuationFactory {
public:
    ActuationFactory() = default;
    ~ActuationFactory() = default;

    std::unique_ptr<robot::actuation::ActuationInterface> CreateActuator(const robot::actuation::Actuator& actuator_config)
    {
        // TODO: Fix this nested switch case. Probably should make comm_factory.
        switch (actuator_config.actuation_type())
        {
            case robot::actuation::ActuationType::STS3215:
            {
                switch (actuator_config.comm_type()){
                    case robot::comm_interface::CommType::SERIAL:
                    {
                        auto serial = robot::comm_interface::CommFactory::GetInstance().GetSerial(actuator_config.serial_config());
                        return std::make_unique<robot::actuation::Sts3215Driver>(serial, actuator_config);
                    }
                }
            }
            default:
                return nullptr;
        }
    }
};
}
