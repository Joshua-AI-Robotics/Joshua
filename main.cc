#include "robot/actuation/factory/actuation_factory.h"
#include "robot/actuation/motors/drivers/sts3215_driver.h"
#include "robot/config/robot.pb.h"
#include "robot/config/config_utils.h"
#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/interfaces/perception_interface.h"
#include "robot/nexus/nexus.h"
#include <map>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <vector>
#include <unistd.h> // For sleep
#include <chrono>
#include <thread>
#include <csignal>
#include <atomic>

std::atomic<bool> quit(false);

void signal_handler(int signal) {
    quit = true;
}


int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;

    signal(SIGINT, signal_handler);

    gflags::ParseCommandLineFlags(&argc, &argv, true);

    robot::Robot robot_config = robot::config_util::LoadRobotConfig("robot/config/robot_config.pbtxt");
    LOG(INFO) << "Robot Name: " << robot_config.name();
    LOG(INFO) << "ID:" << robot_config.id();

    const int trigger_frequency = 30;
    robot::nexus::Nexus nexus(trigger_frequency);

    // Register actuators.
    robot::actuation::ActuationFactory actuator_factory;
    for (const auto& single_actuation : robot_config.actuations().single_actuation()){
        const auto& actuator_proto = single_actuation.actuator();
        nexus.Register(actuator_factory.CreateActuator(actuator_proto));
    }    

    // Register sensors.
    robot::perception::PerceptionFactory perception_factory; 
    for (const auto& single_perception : robot_config.perceptions().single_perception()){
        const auto& sensor_proto = single_perception.sensor();
        nexus.Register(perception_factory.CreatePerception(sensor_proto));
    }

    // Initialize the nexus.
    if (!nexus.Init()) {
        LOG(ERROR) << "Failed to initialize nexus. Exiting.";
        return -1;
    }

    // Start the nexus.
    nexus.Start();

    while(!quit){
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
    LOG(INFO) << "Nexus stopped.";

    
    return 0;
}