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

    std::unique_ptr<robot::action::ActionInterface> CreateAction(const robot::action::Actuator& action_config)
    {
        // TODO: Fix this nested switch case. Probably should make comm_factory.
        switch (action_config.action_type())
        {
            case robot::action::ActionType::STS3215:
            {
                switch (action_config.comm_type()){
                    case robot::comm_interface::CommType::SERIAL:
                    {
                        auto serial = robot::comm_interface::CommFactory::GetInstance().GetSerial(action_config.serial_config());
                        return std::make_unique<robot::action::Sts3215Driver>(serial, action_config);
                    }
                }
            }
            default:
                return nullptr;
        }
    }
};
}
