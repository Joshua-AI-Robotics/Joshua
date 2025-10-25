#pragma once

#include <glog/logging.h>

#include <memory>
#include <string>
#include <vector>

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
  };
  virtual absl::Status SetIdlePosition() {
    LOG(WARNING) << "SetIdlePosition not implemented.";
  };
};
}  // namespace robot::action
