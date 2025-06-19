#include "robot/onboard/interfaces/motor_interface.h"

MotorInterface::MotorInterface() {
    LOG(INFO) << "MotorInterface initialized";
}

MotorInterface::~MotorInterface() {
    LOG(INFO) << "MotorInterface destroyed";
}