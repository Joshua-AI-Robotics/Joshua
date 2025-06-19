#pragma once

#include <glog/logging.h>

// Abstract motor interface.
class MotorInterface{
  public:
    MotorInterface();
    virtual void SetSpeed(int servo_id, float value) = 0;
    virtual void SetPosition(int servo_id, float angle) = 0;
    virtual void SetTorque(int servo_id, float torque) = 0;
    virtual ~MotorInterface();
};