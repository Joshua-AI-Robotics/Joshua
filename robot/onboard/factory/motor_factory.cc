#include "robot/onboard/factory/motor_factory.h"
#include "robot/onboard/drivers/sts3215_driver.h"
#include <memory>

std::unique_ptr<MotorInterface> MotorFactory::CreateMotor(MotorType motor_type){
    switch(motor_type){
        case MotorType::Sts3215: return std::make_unique<Sts3215Driver>(); break;
        default:
            return nullptr;
    }
}