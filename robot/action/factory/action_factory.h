#pragma once

#include "robot/action/interfaces/action_interface.h"
#include "robot/comm_interface/factory/comm_factory.h"
#include "robot/action/motors/drivers/sts3215_driver.h"
#include "config/proto/robot.pb.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include <memory>
#include <string>

namespace robot::action{
class ActionFactory {
public:
    ActionFactory() = default;
    ~ActionFactory() = default;

    absl::StatusOr<std::unique_ptr<robot::action::ActionInterface>> CreateAction(const robot::action::SingleAction& single_action)
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
                                // TODO: 1) Serial should not be hardcoded. 2) Serial should have Create or StatusOr.
                                auto serial = robot::comm_interface::CommFactory::GetInstance().GetSerial(actuator.serial_config());
                                auto driver = std::make_unique<robot::action::Sts3215Driver>(serial, actuator);
                                if(!driver->Init().ok()) {
                                    return absl::Status(absl::StatusCode::kInternal, "Failed to init driver.");
                                }
                                return driver;
                            }
                        }
                    }
                    default:
                        return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid actuator type.");
                }
            }
            // TODO: Add other action types here when they are implemented
            // case robot::action::ActionType::GRIPPER:
            // case robot::action::ActionType::END_EFFECTOR:
            default:
                return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid action type.");
        }
    }
};
}
