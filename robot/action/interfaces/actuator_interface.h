#pragma once

#include <glog/logging.h>

#include <memory>
#include <string>
#include <vector>

#include "robot/action/interfaces/action_interface.h"
#include "utils/robot_time.h"

// Abstract actuator interface.
namespace robot::action {
// Expose the process-global clock to drivers through their shared interface.
// This declaration adds no per-driver clock state or initialization.
using ::joshua::RobotTime;

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
};
}  // namespace robot::action
