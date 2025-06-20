#include "robot/onboard/factory/motor_factory.h"
#include "robot/onboard/drivers/xbox_controller/xbox_controller.h"
#include "robot/onboard/drivers/sts3215_driver.h"
#include <glog/logging.h>
#include <vector>
#include <unistd.h> // For sleep
#include <chrono>
#include <thread>
#include <atomic>   // For std::atomic
#include <csignal>  // For signal handling
#include <memory>   // For std::unique_ptr

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1; // Log messages to stderr

    LOG(INFO) << "Starting main_program";

    try {
        // sudo usermod -a -G dialout $USER
        // And reboot your machine for update the permissions.
        MotorFactory motor_factory;        
       
        boost::asio::io_context io_context;
        auto serial = std::make_shared<Serial>(io_context, "/dev/ttyACM0", 1000000);
        std::vector<std::unique_ptr<MotorInterface>> so100;
        std::vector<int> current_servo_positions(NUMBER_OF_SERVOS);

        for(int i = 0; i < NUMBER_OF_SERVOS; i++){
            so100.emplace_back(motor_factory.CreateMotor(MotorType::STS3215, serial, START_ID + i));
        }
       
        for(int i = 0; i < NUMBER_OF_SERVOS; ++i){
            auto& servo = so100[i];
            servo->SetTorque(1);
            servo->SetSpeed(i == 5 ? SETUP_MOVE_SPEED*2 : SETUP_MOVE_SPEED);
            int middle_position = (SERVO_LIMITS[i].min + SERVO_LIMITS[i].max) / 2;
            current_servo_positions[i] = middle_position;
            servo->SetPosition(middle_position);
        }
        sleep(SETUP_TIME);

        LOG(INFO) << "Shutting down...";
        for (int i = 0; i < NUMBER_OF_SERVOS; ++i) {
            auto& servo = so100[i];
            servo->SetPosition(END_POSITIONS[i]);            
        }
        sleep(SETUP_TIME);
        
        LOG(INFO) << "Disabling torque on all servos...";
        for(auto& servo : so100){
            servo->SetTorque(0);
        }

        controller_thread.join(); // Join the controller thread before exiting

    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }

    

    return 0;
}
