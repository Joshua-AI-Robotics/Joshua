#pragma once

// Abstract motor interface.
class MotorInterface{
  public:
    virtual void SetSpeed(flaot value);
    virtual void SetPosition(float angle);
    virtual void SetTorque(float torque);
    virtual ~MotorInterface() = default;
}