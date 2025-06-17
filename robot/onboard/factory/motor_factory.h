#pragam once

#include "onboard/interface/sts3215.h"

#include <memory>
#include <string>

enum class MotorType{
    Sts3215,
    Invaid
}

class MotorFactory {
    public:
    static std:unique_ptr<MotorInterface> CreateMotor();
}