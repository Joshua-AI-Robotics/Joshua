#pragma once

#include <vector>
#include <string>
#include <glog/logging.h>
#include <memory>
#include "robot/action/interfaces/action_interface.h"

// Abstract actuator interface.
namespace robot::action{
class ActuatorInterface : public ActionInterface{
  public:
    ActuatorInterface() = default;
    virtual ~ActuatorInterface() = default;
    
    // Actuator-specific interface methods
    virtual absl::Status SetSpeed(float value) = 0;
    virtual absl::Status SetPosition(float angle) = 0;
    virtual absl::Status SetTorque(float torque) = 0;
    virtual absl::Status SetMiddlePosition(){ LOG(WARNING) << "SetMiddlePosition not implemented.";};
    virtual absl::Status SetIdlePosition(){ LOG(WARNING) << "SetIdlePosition not implemented.";};
    // TODO: Add this in the config. (e.g. what's the idle position, what's the torque, speed, etc.)
    virtual absl::Status GracefulShutdown(){ LOG(WARNING) << "GracefulShutdown not implemented.";};
};
}