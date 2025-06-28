#include "robot/actuation/factory/motor_factory.h"
#include "robot/actuation/motors/drivers/sts3215_driver.h"
#include "robot/config/robot.pb.h"
#include "robot/config/config_utils.h"
#include "robot/perception/factory/camera_factory.h"
#include "robot/perception/interfaces/camera_interface.h"
#include <opencv2/highgui.hpp>
#include "utils/so100_xbox_controller_handler.h"
#include <map>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <vector>
#include <unistd.h> // For sleep
#include <chrono>
#include <thread>

namespace{
    constexpr int kSetupTime = 2;
}

DEFINE_bool(enable_camera, false, "Enable camera.");
DEFINE_bool(enable_controller, false, "Enable controller.");

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    gflags::ParseCommandLineFlags(&argc, &argv, true);

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
        std::vector<std::unique_ptr<robot::perception::CameraInterface>> cameras;

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

        // Initialize and start So100XboxControllerHandler
        utils::So100XboxControllerHandler controller_handler(robot_config, motors);
        if(FLAGS_enable_controller){
            if (!controller_handler.Init()) {
                LOG(ERROR) << "Failed to initialize So100XboxControllerHandler. Exiting.";
                return 1;
            }

            std::thread controller_thread([&controller_handler]() {
                controller_handler.Start();
            });
            controller_thread.detach();
        }

        // Initialize camera
        if(FLAGS_enable_camera){
            robot::perception::CameraFactory camera_factory;
            for (const auto& single_perception : robot_config.perceptions().single_perception()){
                const auto& camera_proto = single_perception.camera();
                cameras.emplace_back(camera_factory.CreateCamera(camera_proto));
            }

            cv::namedWindow("Camera", cv::WINDOW_AUTOSIZE);

            while (true) {
                cv::Mat frame = cameras[0]->Capture();
                if (frame.empty()) {
                    LOG(WARNING) << "Failed to capture frame.";
                    break;
                }
                cv::imshow("Camera", frame);
                if (cv::waitKey(1) == 'q') {
                    break;
                }
            }
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

        cv::destroyAllWindows();


    } catch (const std::exception& e) {
        LOG(ERROR) << "Error: " << e.what();
        return 1;
    }
    
    return 0;
}
