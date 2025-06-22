#include "robot/actuation/interfaces/motor_interface.h"

namespace robot::actuation {

MotorInterface::MotorInterface() {
    LOG(INFO) << "MotorInterface initialized";
}

MotorInterface::~MotorInterface() {
    LOG(INFO) << "MotorInterface destroyed";
}

} // namespace robot::actuation