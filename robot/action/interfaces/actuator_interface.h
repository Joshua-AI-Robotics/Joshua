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
    virtual void SetSpeed(float value) = 0;
    virtual void SetPosition(float angle) = 0;
    virtual void SetTorque(float torque) = 0;
    virtual void SetMiddlePosition(){ LOG(WARNING) << "SetMiddlePosition not implemented.";};
    virtual void SetIdlePosition(){ LOG(WARNING) << "SetIdlePosition not implemented.";};
    // TODO: Add this in the config. (e.g. what's the idle position, what's the torque, speed, etc.)
    virtual void GracefulShutdown(){ LOG(WARNING) << "GracefulShutdown not implemented.";};
};
}