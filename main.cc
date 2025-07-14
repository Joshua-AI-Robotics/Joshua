#include "robot/actuation/factory/actuation_factory.h"
#include "robot/actuation/motors/drivers/sts3215_driver.h"
#include "config/proto/config.pb.h"
#include "config/config_utils.h"
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

    config::Config config = config::config_util::LoadConfig("config/config_preset/so100_with_dt.pbtxt");
    auto robot_config = config.robot();
    auto ai_config = config.ai();
    LOG(INFO) << "Robot Name: " << robot_config.name();
    LOG(INFO) << "ID:" << robot_config.id();

    robot::nexus::Nexus nexus(config);

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

    // Start LeRobot dataset collection for fake training
    LOG(INFO) << "Starting LeRobot dataset collection for fake training...";
    nexus.StartLeRobotDatasetCollection();

    // Simulate training data collection
    int training_episodes = 2;  // Number of episodes to collect
    int episode_duration = 10;  // Seconds per episode
    
    LOG(INFO) << "Collecting " << training_episodes << " episodes of training data...";
    LOG(INFO) << "Each episode will run for " << episode_duration << " seconds.";
    LOG(INFO) << "Press Ctrl+C to stop early.";

    for (int episode = 0; episode < training_episodes && !quit; episode++) {
        LOG(INFO) << "=== Training Episode " << (episode + 1) << "/" << training_episodes << " ===";
        
        // Run episode for specified duration
        auto start_time = std::chrono::steady_clock::now();
        while (!quit) {
            auto current_time = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - start_time);
            
            if (elapsed.count() >= episode_duration) {
                break;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        if (!quit) {
            LOG(INFO) << "Episode " << (episode + 1) << " completed.";
        }
    }

    if (!quit) {
        // Save the collected training dataset
        LOG(INFO) << "Training data collection completed!";
        LOG(INFO) << "Saving LeRobot dataset...";
        nexus.SaveLeRobotDataset("/home/hmoon/ProjectJoshuaTrainingData");
        LOG(INFO) << "Dataset saved to 'fake_training_dataset' directory.";
        LOG(INFO) << "This dataset can be used for supervised learning training.";
    } else {
        LOG(INFO) << "Training interrupted by user.";
    }
    
    LOG(INFO) << "Fake training completed.";

    
    return 0;
}