#include "robot/onboard/interfaces/motor_interface.h"

namespace robot::onboard {

MotorInterface::MotorInterface() {
    LOG(INFO) << "MotorInterface initialized";
}

MotorInterface::~MotorInterface() {
    LOG(INFO) << "MotorInterface destroyed";
}

} // namespace robot::onboard