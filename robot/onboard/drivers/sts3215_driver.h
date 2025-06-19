#pragma once

#include <boost/asio.hpp>
#include "robot/onboard/interfaces/motor_interface.h"

class Sts3215Driver : public MotorInterface {
  public:
  Sts3215Driver();
  void SetSpeed(float value) override;
  void SetPosition(float angle) override;
  void SetTorque(float torque) override;

  private:

  
};