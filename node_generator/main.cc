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

    // Clean Bazel cache to resolve module dependency issues
    LOG(INFO) << "Cleaning Bazel cache...";
    std::string clean_command = "bazel clean --expunge";
    int clean_exit = system(clean_command.c_str());
    if (clean_exit != 0) {
        LOG(WARNING) << "Failed to clean Bazel cache, continuing anyway...";
    }

    // Build the nodes with bazel build from repository root
    LOG(INFO) << "Building ros2:actuation_subscriber...";
    std::string bazel_build_command = "bazel build ros2:actuation_subscriber --verbose_failures";
    int exit_code = system(bazel_build_command.c_str());
    if (exit_code != 0) {
        LOG(ERROR) << "Failed to build the nodes with bazel build.";
        return 1;
    }

    // Locate the wrapper script in bazel-bin after building
    std::filesystem::path wrapper_script_path = std::filesystem::path(repo_root) / "bazel-bin" / "ros2" / "actuation_subscriber_wrapper.sh";
    
    // Check if wrapper script exists
    if (!std::filesystem::exists(wrapper_script_path)) {
        LOG(ERROR) << "Wrapper script not found at: " << wrapper_script_path;
        return 1;
    }
    
    // Execute the wrapper script with config parameter
    std::string command = wrapper_script_path.string() + " " + FLAGS_config;
    LOG(INFO) << "Executing: " << command;
    exit_code = system(command.c_str());
    if (exit_code != 0) {
        LOG(ERROR) << "Failed to run the nodes from wrapper scripts.";
        return 1;
    }

    return 0;
}
