#include "robot/onboard/factory/motor_factory.h"
#include "robot/onboard/drivers/sts3215_driver.h"
#include "robot/config/robot.pb.h"
#include <map>
#include <glog/logging.h>
#include <vector>
#include <unistd.h> // For sleep
#include <chrono>
#include <thread>
#include <google/protobuf/text_format.h>
#include <fstream>

namespace{
    constexpr int kSetupTime = 2;
}

robot_config::Robot LoadRobotConfig(const std::string& config_path) {
    robot_config::Robot robot_config;
    std::ifstream input(config_path);
    if (!input) {
        LOG(ERROR) << "Failed to open robot config file: " << config_path;
        throw std::runtime_error("Failed to open robot config file: " + config_path);
    }
    std::string config_content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (!google::protobuf::TextFormat::ParseFromString(config_content, &robot_config)) {
        LOG(ERROR) << "Failed to parse robot config from file: " << config_path;
        throw std::runtime_error("Failed to parse robot config from file: " + config_path);
    }
    return robot_config;
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    LOG(INFO) << "Starting main_program";

    try {
        // sudo usermod -a -G dialout $USER
        // And reboot your machine for update the permissions.        
        robot_config::Robot robot_config = LoadRobotConfig("robot/config/robot_config.pbtxt");
        auto number_of_motors = robot_config.motor_size();
        LOG(INFO) << "Robot Name: " << robot_config.name();
        LOG(INFO) << "ID:" << robot_config.id();
               
                
        // Motor instantiation.
        robot::onboard::MotorFactory motor_factory; 
        std::vector<std::unique_ptr<robot::onboard::MotorInterface>> motors;

        for(int i = 0; i < number_of_motors; i++){
            const auto& motor_proto = robot_config.motor(i);

            switch(motor_proto.motor_type()){
                case robot_config::MotorType::STS3215:
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
