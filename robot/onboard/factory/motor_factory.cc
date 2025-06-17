#include "motor_factory.h"
#include "onboard/driver/motor_driver.h"

std::unique_ptr<MotorInterface> MotorFactory::CreateMotor(MotorType motor_type){
    switch(motor_type){
        case MotorType::Sts3215: return std::make_unique<Sts3215Driver>(); break;
        default:
            break;
    }
}