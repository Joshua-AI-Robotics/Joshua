#pragma once

#include "onboard/interface/motor_interface.h"

class Sts3215Driver : public MotorInterface {
  public:
  Sts3215Driver();
  void SetSpeed() override;
  void SetPosition() override;
  void SetTorque() override;

  private:
  
}