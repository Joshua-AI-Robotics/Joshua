#pragma once

#include <glog/logging.h>

// Abstract motor interface.
class MotorInterface{
  public:
    MotorInterface();
    virtual void SetSpeed(float value) = 0;
    virtual void SetPosition(float angle) = 0;
    virtual void SetTorque(float torque) = 0;
    virtual ~MotorInterface();
};