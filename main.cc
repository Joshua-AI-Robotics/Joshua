#include "robot/actuation/factory/motor_factory.h"
#include "robot/actuation/motors/drivers/sts3215_driver.h"
#include "robot/config/robot.pb.h"
#include "robot/config/config_utils.h"
#include <map>
#include <glog/logging.h>
#include <vector>
#include <unistd.h> // For sleep
#include <chrono>
#include <thread>

namespace{
    constexpr int kSetupTime = 2;
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    LOG(INFO) << "Starting main_program";

    try {
        // sudo usermod -a -G dialout $USER
        // And reboot your machine for update the permissions.        
        robot::Robot robot_config = robot::config_util::LoadRobotConfig("robot/config/robot_config.pbtxt");
        auto number_of_motors = robot_config.actuations().single_actuation_size();
        LOG(INFO) << "Robot Name: " << robot_config.name();
        LOG(INFO) << "ID:" << robot_config.id();
               
                
        // Motor instantiation.
        robot::actuation::MotorFactory motor_factory; 
        std::vector<std::unique_ptr<robot::actuation::MotorInterface>> motors;

        for(int i = 0; i < number_of_motors; i++){
            const auto& single_actuation = robot_config.actuations().single_actuation(i);
            const auto& motor_proto = single_actuation.motor();

            switch(motor_proto.motor_type()){
                case robot::actuation::MotorType::STS3215:
                    motors.emplace_back(motor_factory.CreateMotor(motor_proto));
                    break;
                default:
                    LOG(ERROR) << "Unknown motor type: " << motor_proto.motor_type();
                    break;
            }
        }
       
        // Random servo movements for validation.
        for(int i = 0; i < number_of_motors; ++i){
            auto& servo = motors[i];
            servo->SetTorque(1);
            servo->SetMiddlePosition();
        }
        sleep(kSetupTime);

        for(int i = 5; i > 0; --i){   
            system("clear");         
            LOG(INFO) << "Shutting down in " << i << "...";
            for(int i = 0; i < number_of_motors; i++){
                LOG(INFO) << "Servo [" << i  << "] value: " << motors[i]->GetPosition();
            }
            sleep(1);            
        }


        LOG(INFO) << "Shutting down...";
        for (int i = 0; i < number_of_motors; ++i) {
            auto& servo = motors[i];
            servo->SetIdlePosition();            
        }
        sleep(kSetupTime);
        
        LOG(INFO) << "Disabling torque on all servos...";
        for(auto& servo : motors){
            servo->SetTorque(0);
        }

    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }
    
    return 0;
}
