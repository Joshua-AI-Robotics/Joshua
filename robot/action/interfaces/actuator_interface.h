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
  // For position-only revolute drivers. Validate the entire requested command
  // before issuing any channel writes; velocity is not a speed-limit setting and
  // effort must never become an enable/disable gate. Metadata is informational.
  static absl::StatusOr<float> JointPositionInNativeUnits(const ActionPacket& packet,
                                                          const std::string& joint_name,
                                                          double units_per_radian) {
    const auto& command = packet.joint_command();
    if (packet.normalized() || joint_name.empty() || command.joint_name() != joint_name)
      return absl::InvalidArgumentError("Joint command requires matching name and SI units");
    if (command.has_velocity() || command.has_effort())
      return absl::UnimplementedError(
          "Driver supports JointState position only, not velocity/effort");
    if (!command.has_position() || !std::isfinite(command.position()))
      return absl::InvalidArgumentError("Joint command requires a finite position");
    const long double native = static_cast<long double>(command.position()) * units_per_radian;
    if (!std::isfinite(native) || native < -std::numeric_limits<float>::max() ||
        native > std::numeric_limits<float>::max())
      return absl::OutOfRangeError("Joint position exceeds native float range");
    const float value = static_cast<float>(native);
    if (native != 0 && value == 0)
      return absl::OutOfRangeError("Joint position underflows native float range");
    return value;
  }
};
}  // namespace robot::action
