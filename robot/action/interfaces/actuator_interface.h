#pragma once

#include <glog/logging.h>

#include <cmath>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
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
  // Validate the complete payload before any device writes. NATIVE velocity is
  // the driver's existing move-speed setting; SI velocity is physical velocity.
  static absl::StatusOr<JointCommand> NativeJointCommand(const ActionPacket& packet,
                                                         const std::string& joint_name,
                                                         double units_per_radian,
                                                         bool supports_native_effort,
                                                         float lower_limit,
                                                         float upper_limit) {
    auto command = packet.joint();
    if (packet.normalized() || joint_name.empty() || command.joint_name() != joint_name)
      return absl::InvalidArgumentError(
          "Joint command requires matching name and denormalized values");
    if (!command.has_position() && !command.has_velocity() && !command.has_effort())
      return absl::InvalidArgumentError("Joint command is empty");
    if (command.units() == JointCommand::SI) {
      if (units_per_radian <= 0 || command.has_velocity() || command.has_effort())
        return absl::UnimplementedError(
            "Driver has no requested SI velocity/effort or position contract");
      command.set_position(command.position() * units_per_radian);
      command.set_units(JointCommand::NATIVE);
    } else if (command.units() != JointCommand::NATIVE) {
      return absl::InvalidArgumentError("Unknown joint command units");
    }
    if (command.has_effort() && !supports_native_effort)
      return absl::UnimplementedError(
          "Driver has no effort control; use presets for torque enable/disable");
    for (double value : {command.position(), command.velocity(), command.effort()}) {
      if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max() ||
          (value != 0 && static_cast<float>(value) == 0))
        return absl::OutOfRangeError("Joint value exceeds native float range");
    }
    if (command.has_position() &&
        (command.position() < lower_limit || command.position() > upper_limit))
      return absl::InvalidArgumentError("Joint position is outside operational limits");
    if ((command.has_velocity() && command.velocity() < 0) ||
        (command.has_effort() && command.effort() < 0))
      return absl::InvalidArgumentError("Driver requires nonnegative native speed and effort");
    return command;
  }
};
}  // namespace robot::action
