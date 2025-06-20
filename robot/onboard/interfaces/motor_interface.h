#pragma once

#include <glog/logging.h>

// Abstract motor interface.
namespace robot::onboard{
class MotorInterface{
  public:
    MotorInterface();
    virtual void SetSpeed(float value) = 0;
    virtual void SetPosition(float angle) = 0;
    virtual void SetTorque(float torque) = 0;
    virtual float GetPosition() = 0;
    virtual void SetMiddlePosition(){ LOG(WARNING) << "SetMiddlePosition not implemented.";};
    virtual void SetIdlePosition(){ LOG(WARNING) << "SetIdlePosition not implemented.";};
    virtual ~MotorInterface();
};
}