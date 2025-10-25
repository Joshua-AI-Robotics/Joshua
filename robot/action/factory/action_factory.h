#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/action_interface.h"
#include "robot/action/motors/drivers/sts3215_driver.h"
#include "robot/comm/factory/comm_factory.h"

namespace robot::action {
class ActionFactory {
 public:
  static absl::StatusOr<std::unique_ptr<robot::action::ActionInterface>> CreateAction(
      const robot::action::SingleAction& single_action) {
    switch (single_action.action_type()) {
      case robot::action::ActionType::ACTUATOR: {
        const auto& actuator = single_action.actuator();
        switch (actuator.actuator_type()) {
          case robot::action::ActuatorType::STS3215_SERVO: {
            auto serial = robot::comm::CommFactory::CreateSerial(actuator.comm());
            if (!serial.ok()) {
              return serial.status();
            }

            auto driver = std::make_unique<robot::action::Sts3215Driver>(serial.value(), actuator);
            if (!driver->Init().ok()) {
              return absl::Status(absl::StatusCode::kInternal, "Failed to init driver.");
            }
            return driver;
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

  ~ActionFactory() = default;
  ActionFactory(const ActionFactory&) = delete;
  ActionFactory& operator=(const ActionFactory&) = delete;
  ActionFactory(ActionFactory&&) = default;
  ActionFactory& operator=(ActionFactory&&) = default;

 private:
  ActionFactory() = default;
};
}  // namespace robot::action
