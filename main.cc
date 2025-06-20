#include "robot/onboard/factory/motor_factory.h"
#include "robot/onboard/drivers/sts3215_driver.h"
#include <map>
#include <glog/logging.h>
#include <vector>
#include <unistd.h> // For sleep
#include <chrono>
#include <thread>

namespace{
    constexpr int kNumberOfServo = 6;
    constexpr int kStartId= 1;
    constexpr int kSetupMoveSpeed = 1200;
    constexpr int kSetupTime = 2;
    struct ServoLimit {
        int min;
        int max;
    };

    const std::vector<float> kEndPosition = {2070, 847, 3011, 655, 1838, 1806};

    const std::map<int, ServoLimit> kServoLimits = {
        {0, {1024, 3072}},
        {1, {800, 3000}},
        {2, {950, 3000}},
        {3, {900, 3072}},
        {4, {0, 3000}},
        {5, {1762, 2400}},
    };
}

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    LOG(INFO) << "Starting main_program";

    try {
        // sudo usermod -a -G dialout $USER
        // And reboot your machine for update the permissions.
        
        robot::onboard::MotorFactory motor_factory;        
       
        boost::asio::io_context io_context;
        auto serial = std::make_shared<robot::comm_interface::Serial>(io_context, "/dev/ttyACM0", 1000000);
        std::vector<std::unique_ptr<robot::onboard::MotorInterface>> so100;
        std::vector<int> current_servo_positions(kNumberOfServo);

        for(int i = 0; i < kNumberOfServo; i++){
            so100.emplace_back(motor_factory.CreateMotor<robot::comm_interface::Serial>(robot::onboard::MotorType::STS3215, serial, kStartId + i));
        }
       
        for(int i = 0; i < kNumberOfServo; ++i){
            auto& servo = so100[i];
            servo->SetTorque(1);
            servo->SetSpeed(i == 5 ? kSetupMoveSpeed*2 : kSetupMoveSpeed);
            int middle_position = (kServoLimits.at(i).min + kServoLimits.at(i).max) / 2;
            current_servo_positions[i] = middle_position;
            servo->SetPosition(middle_position);
        }
        sleep(kSetupTime);

        for(int i = 5; i > 0; --i){
            LOG(INFO) << "Shutting down in " << i << "...";
            sleep(1);
        }

        LOG(INFO) << "Shutting down...";
        for (int i = 0; i < kNumberOfServo; ++i) {
            auto& servo = so100[i];
            servo->SetPosition(kEndPosition[i]);            
        }
        sleep(kSetupTime);
        
        LOG(INFO) << "Disabling torque on all servos...";
        for(auto& servo : so100){
            servo->SetTorque(0);
        }

    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }
    
    return 0;
}
