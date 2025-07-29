#pragma once

#include "robot/action/interfaces/action_interface.h"
#include "robot/comm_interface/factory/comm_factory.h"
#include "robot/action/motors/drivers/sts3215_driver.h"
#include "config/proto/robot.pb.h"

#include <memory>
#include <string>

namespace robot::action{
class ActionFactory {
public:
    ActionFactory() = default;
    ~ActionFactory() = default;

    std::unique_ptr<robot::action::ActionInterface> CreateAction(const robot::action::SingleAction& single_action)
    {
        switch (single_action.action_type())
        {
            case robot::action::ActionType::ACTUATOR:
            {
                const auto& actuator = single_action.actuator();
                switch (actuator.actuator_type())
                {
                    case robot::action::ActuatorType::STS3215_SERVO:
                    {
                        switch (actuator.comm_type()){
                            case robot::comm_interface::CommType::SERIAL:
                            {
                                auto serial = robot::comm_interface::CommFactory::GetInstance().GetSerial(actuator.serial_config());
                                return std::make_unique<robot::action::Sts3215Driver>(serial, actuator);
                            }
                        }
                    }
                    default:
                        return nullptr;
                }
            }
            // TODO: Add other action types here when they are implemented
            // case robot::action::ActionType::GRIPPER:
            // case robot::action::ActionType::END_EFFECTOR:
            default:
                return nullptr;
        }
    }
};
}
