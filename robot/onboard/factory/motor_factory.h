#pragma once

#include "robot/onboard/interfaces/motor_interface.h"

#include <memory>
#include <string>

enum class MotorType{
    Sts3215,
    Invalid
};

class MotorFactory {
    public:
    static std::unique_ptr<MotorInterface> CreateMotor(MotorType motor_type);
};