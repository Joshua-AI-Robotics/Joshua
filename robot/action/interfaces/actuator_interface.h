#pragma once

#include <glog/logging.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "robot/action/interfaces/action_interface.h"

// Abstract actuator interface.
namespace robot::action {
class ActuatorInterface : public ActionInterface {
 public:
  ActuatorInterface() = default;
  virtual ~ActuatorInterface() = default;

  // Actuator-specific interface methods
  virtual absl::Status SetSpeed(float value) = 0;
  virtual absl::Status SetPosition(float angle) = 0;
  virtual absl::Status SetTorque(float torque) = 0;
  virtual absl::Status SetMiddlePosition() {
    LOG(WARNING) << "SetMiddlePosition not implemented.";
    return absl::OkStatus();
  };
  virtual absl::Status SetIdlePosition() {
    LOG(WARNING) << "SetIdlePosition not implemented.";
    return absl::OkStatus();
  };

 protected:
  // Hardware-independent validation only. Each driver owns capability checks,
  // unit conversions, numeric precision and operational limits.
  static absl::Status ValidateJointCommand(const JointCommand& command,
                                           const std::string& joint_name) {
    if (joint_name.empty() || command.joint_name() != joint_name)
      return absl::InvalidArgumentError("Joint command requires matching name");
    if (!command.has_position() && !command.has_velocity() && !command.has_effort())
      return absl::InvalidArgumentError("Joint command is empty");
    if (!JointCommand::Units_IsValid(command.units()))
      return absl::InvalidArgumentError("Unknown velocity/effort units");
    if (!JointCommand::PositionEncoding_IsValid(command.position_encoding()))
      return absl::InvalidArgumentError("Unknown position encoding");
    for (double value : {command.position(), command.velocity(), command.effort()}) {
      if (!std::isfinite(value))
        return absl::InvalidArgumentError("Joint command values must be finite");
    }
    return absl::OkStatus();
  }
};
}  // namespace robot::action
