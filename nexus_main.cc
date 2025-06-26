#include "robot/actuation/factory/motor_factory.h"
#include "robot/actuation/motors/drivers/sts3215_driver.h"
#include "robot/config/robot.pb.h"
#include "robot/config/config_utils.h"
#include "robot/perception/factory/camera_factory.h"
#include "robot/perception/interfaces/camera_interface.h"
#include "robot/nexus/nexus.h"
#include <map>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <vector>
#include <unistd.h> // For sleep
#include <chrono>
#include <thread>

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    robot::Robot robot_config = robot::config_util::LoadRobotConfig("robot/config/robot_config.pbtxt");
    LOG(INFO) << "Robot Name: " << robot_config.name();
    LOG(INFO) << "ID:" << robot_config.id();

    robot::nexus::Nexus nexus;
    nexus.Init();
    robot::actuation::MotorFactory motor_factory;
    for (const auto& single_actuation : robot_config.actuations().single_actuation()){
        const auto& motor_proto = single_actuation.motor();
        nexus.Register(motor_factory.CreateMotor(motor_proto));
    }    

    robot::perception::CameraFactory camera_factory; 
    for (const auto& single_perception : robot_config.perceptions().single_perception()){
        const auto& camera_proto = single_perception.camera();
        nexus.Register(camera_factory.CreateCamera(camera_proto));
    }

    nexus.Start();

    while(true){
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    
}