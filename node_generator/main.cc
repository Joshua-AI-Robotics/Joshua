#include "config/proto/config.pb.h"
#include "config/config_utils.h"
#include <map>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <vector>
#include <chrono>
#include <thread>
#include <filesystem>
#include <unistd.h>

DEFINE_string(config, "config/config_preset/so100_with_follower.pbtxt", "Path to the robot config file");

int main(int argc, char* argv[]) {
    google::InitGoogleLogging(argv[0]);
    FLAGS_logtostderr = 1;
    gflags::ParseCommandLineFlags(&argc, &argv, true);    

    config::Config config = config::config_util::LoadConfig(FLAGS_config);
    auto robot_config = config.robot();
    auto ai_config = config.ai();

    LOG(INFO) << "Robot Name: " << robot_config.name();
    LOG(INFO) << "Actuator Size: " << robot_config.actuations().single_actuation_size();
    LOG(INFO) << "Perception Size: " << robot_config.perceptions().single_perception_size();

    // Find the repository root directory
    std::string repo_root = "/home/hmoon/Projects/ProjectJoshua";
    
    LOG(INFO) << "Repository root: " << repo_root;

    // Change to the repository root directory to run bazel build
    if (chdir(repo_root.c_str()) != 0) {
        LOG(ERROR) << "Failed to change to repository root: " << repo_root;
        return 1;
    }

    bool has_camera = false;
    bool has_encoder = false;
    bool has_actuator = false;

    for (const auto& single_perception : robot_config.perceptions().single_perception()) {
        if (single_perception.has_sensor()) {
            if (single_perception.sensor().sensor_config_case() == robot::perception::Sensor::kCameraConfig) {
                has_camera = true;
            }
            if (single_perception.sensor().sensor_config_case() == robot::perception::Sensor::kEncoderConfig) {
                has_encoder = true;
            }
        }
    }

    for (const auto& single_actuation : robot_config.actuations().single_actuation()) {
        if (single_actuation.has_actuator()) {
            has_actuator = true;
        }
    }

    std::filesystem::path wrapper_script_path = std::filesystem::path(repo_root) / "bazel-bin" / "ros2";

    //TODO: Implement what nodes need to be built based on the config.
    //TODO: Update the config to contain the node names, topics, and parameters.

    // Build all required nodes first (sequential to avoid build conflicts)
    std::vector<std::string> nodes_to_run;
    
    if (has_camera) {
        LOG(INFO) << "Building ros2:camera_publisher...";
        std::string bazel_build_command = "bazel build ros2:camera_publisher";
        int exit_code = system(bazel_build_command.c_str());
        if (exit_code != 0) {
            LOG(ERROR) << "Failed to build camera_publisher with bazel build.";
            return 1;
        }
        nodes_to_run.push_back("camera_publisher");
    }

    if (has_encoder) {
        LOG(INFO) << "Building ros2:encoder_publisher...";
        std::string bazel_build_command = "bazel build ros2:encoder_publisher";
        int exit_code = system(bazel_build_command.c_str());
        if (exit_code != 0) {
            LOG(ERROR) << "Failed to build encoder_publisher with bazel build.";
            return 1;
        }
        nodes_to_run.push_back("encoder_publisher");
    }

    if (has_actuator) {
        LOG(INFO) << "Building ros2:actuation_subscriber...";
        std::string bazel_build_command = "bazel build ros2:actuation_subscriber";
        int exit_code = system(bazel_build_command.c_str());
        if (exit_code != 0) {
            LOG(ERROR) << "Failed to build actuation_subscriber with bazel build.";
            return 1;
        }
        nodes_to_run.push_back("actuation_subscriber");
    }

    // Launch all nodes in parallel using threads
    std::vector<std::thread> node_threads;
    
    for (const auto& node_name : nodes_to_run) {
        node_threads.emplace_back([&wrapper_script_path, &node_name, &FLAGS_config]() {
            std::string command = wrapper_script_path.string() + "/" + node_name + "_wrapper.sh " + FLAGS_config;
            LOG(INFO) << "Executing in parallel: " << command;
            int exit_code = system(command.c_str());
            if (exit_code != 0) {
                LOG(ERROR) << "Node " << node_name << " exited with error code: " << exit_code;
            } else {
                LOG(INFO) << "Node " << node_name << " completed successfully.";
            }
        });
    }

    LOG(INFO) << "All " << node_threads.size() << " nodes launched in parallel.";
    
    // Wait for all node threads to complete (or run indefinitely)
    for (auto& thread : node_threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    
    return 0;
}
