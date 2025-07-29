#include "config/proto/config.pb.h"
#include "config/config_utils.h"
#include <map>
#include <set>
#include <glog/logging.h>
#include <gflags/gflags.h>
#include <vector>
#include <chrono>
#include <thread>
#include <filesystem>
#include <unistd.h>
#include <sstream>
#include <sys/wait.h>
#include <signal.h>

DEFINE_string(config, "config/config_preset/so100_with_follower.pbtxt", "Path to the robot config file");

// Global variable for signal handling
volatile sig_atomic_t shutdown_requested = 0;

void signal_handler(int sig) {
    shutdown_requested = 1;
}

// Utility function to launch a perception node process based on type and node_id
pid_t LaunchPerceptionNodeProcess(const std::string& binary_path, const std::string& config_path, 
                                 const std::string& node_type, int node_id, const std::string& node_name) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        // Set up clean environment for ROS2
        std::string ament_path = binary_path + "/" + node_type + "_launch_ament_setup";
        setenv("AMENT_PREFIX_PATH", ament_path.c_str(), 1);
        
        // Change to the correct working directory
        std::string runfiles_dir = binary_path + "/" + node_type + ".runfiles/_main";
        if (chdir(runfiles_dir.c_str()) != 0) {
            LOG(ERROR) << "Failed to change to runfiles directory: " << runfiles_dir;
            _exit(1);
        }
        
        // Execute the binary directly with config path and node_id as arguments
        std::string binary_impl = binary_path + "/" + node_type + "_impl";
        std::string node_id_str = std::to_string(node_id);
        execl(binary_impl.c_str(), binary_impl.c_str(), config_path.c_str(), node_id_str.c_str(), nullptr);
        
        // If we reach here, execl failed
        LOG(ERROR) << "Failed to execute " << binary_impl << ": " << strerror(errno);
        _exit(1);
    } else if (pid > 0) {
        // Parent process
        LOG(INFO) << "Launched " << node_name << " with PID: " << pid;
        return pid;
    } else {
        // Fork failed
        LOG(ERROR) << "Failed to fork process for " << node_name << ": " << strerror(errno);
        return -1;
    }
}

// Utility function to launch actuation node process
pid_t LaunchActuationNodeProcess(const std::string& binary_path, const std::string& config_path, 
                                const std::string& node_name) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        // Set up clean environment for ROS2
        std::string ament_path = binary_path + "/actuation_subscriber_launch_ament_setup";
        setenv("AMENT_PREFIX_PATH", ament_path.c_str(), 1);
        
        // Change to the correct working directory
        std::string runfiles_dir = binary_path + "/actuation_subscriber.runfiles/_main";
        if (chdir(runfiles_dir.c_str()) != 0) {
            LOG(ERROR) << "Failed to change to runfiles directory: " << runfiles_dir;
            _exit(1);
        }
        
        // Execute the binary directly with config path as argument
        std::string binary_impl = binary_path + "/actuation_subscriber_impl";
        execl(binary_impl.c_str(), binary_impl.c_str(), config_path.c_str(), nullptr);
        
        // If we reach here, execl failed
        LOG(ERROR) << "Failed to execute " << binary_impl << ": " << strerror(errno);
        _exit(1);
    } else if (pid > 0) {
        // Parent process
        LOG(INFO) << "Launched " << node_name << " with PID: " << pid;
        return pid;
    } else {
        // Fork failed
        LOG(ERROR) << "Failed to fork process for " << node_name << ": " << strerror(errno);
        return -1;
    }
}

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

    // Group sensors by node_id and determine what type of node each should be
    std::map<uint32_t, std::set<std::string>> node_sensor_types;
    std::set<std::string> required_builds;

    for (const auto& single_perception : robot_config.perceptions().single_perception()) {
        if (single_perception.has_sensor()) {
            uint32_t node_id = single_perception.node_id();
            
            if (single_perception.sensor().sensor_type() == robot::perception::SensorType::CAMERA) {
                node_sensor_types[node_id].insert("camera");
                required_builds.insert("ros2:camera_publisher");
            } else if (single_perception.sensor().sensor_type() == robot::perception::SensorType::ENCODER) {
                node_sensor_types[node_id].insert("encoder");
                required_builds.insert("ros2:encoder_publisher");
            }
        }
    }

    // Check if we have actuators
    bool has_actuator = !robot_config.actuations().single_actuation().empty();
    if (has_actuator) {
        required_builds.insert("ros2:actuation_subscriber");
    }

    LOG(INFO) << "Found " << node_sensor_types.size() << " unique perception node IDs";
    for (const auto& [node_id, sensor_types] : node_sensor_types) {
        LOG(INFO) << "Node ID " << node_id << " contains: ";
        for (const auto& sensor_type : sensor_types) {
            LOG(INFO) << "  - " << sensor_type;
        }
    }

    std::filesystem::path wrapper_script_path = std::filesystem::path(repo_root) / "bazel-bin" / "ros2";

    // Build required binaries
    for (const auto& build_target : required_builds) {
        LOG(INFO) << "Building " << build_target << "...";
        std::string bazel_build_command = "bazel build " + build_target;
        int exit_code = system(bazel_build_command.c_str());
        if (exit_code != 0) {
            LOG(ERROR) << "Failed to build " << build_target << " with bazel build.";
            return 1;
        }
    }

    // Launch all nodes
    std::vector<pid_t> node_pids;

    // Launch perception nodes - one per unique node_id
    for (const auto& [node_id, sensor_types] : node_sensor_types) {
        std::string binary_path = wrapper_script_path.string();
        
        // Determine what type of node to launch based on sensor types
        // For now, we'll prioritize camera > encoder
        // In the future, you might want a unified perception node
        std::string node_type;
        std::string node_name;
        
        if (sensor_types.count("camera")) {
            node_type = "camera_publisher";
            node_name = "camera_publisher_node_" + std::to_string(node_id);
        } else if (sensor_types.count("encoder")) {
            node_type = "encoder_publisher";
            node_name = "encoder_publisher_node_" + std::to_string(node_id);
        } else {
            LOG(WARNING) << "Unknown sensor types for node_id " << node_id;
            continue;
        }
        
        pid_t pid = LaunchPerceptionNodeProcess(binary_path, FLAGS_config, node_type, node_id, node_name);
        if (pid > 0) {
            node_pids.push_back(pid);
        }
    }

    // Launch actuation node (single node for all actuators)
    if (has_actuator) {
        std::string binary_path = wrapper_script_path.string();
        std::string node_name = "actuation_subscriber";
        
        pid_t pid = LaunchActuationNodeProcess(binary_path, FLAGS_config, node_name);
        if (pid > 0) {
            node_pids.push_back(pid);
        }
    }

    LOG(INFO) << "All " << node_pids.size() << " nodes launched successfully.";
    
    // Set up signal handler for graceful shutdown
    auto cleanup_and_exit = [&](int exit_code) {
        LOG(INFO) << "Shutting down all nodes...";
        
        // Terminate all child processes
        for (pid_t pid : node_pids) {
            LOG(INFO) << "Terminating node with PID: " << pid;
            kill(pid, SIGTERM);
        }
        
        // Wait for processes to terminate gracefully
        for (pid_t pid : node_pids) {
            int status;
            if (waitpid(pid, &status, WNOHANG) == 0) {
                // Process still running, force kill after timeout
                sleep(2);
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
        }
        
        LOG(INFO) << "All processes terminated successfully.";
        exit(exit_code);
    };
    
    // Setup signal handling for graceful shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    if (node_pids.empty()) {
        LOG(WARNING) << "No nodes were launched successfully.";
        cleanup_and_exit(1);
    }
    
    // Monitor child processes and handle shutdown requests
    while (!shutdown_requested) {
        for (auto it = node_pids.begin(); it != node_pids.end();) {
            int status;
            pid_t result = waitpid(*it, &status, WNOHANG);
            
            if (result > 0) {
                // Child process has terminated
                if (WIFEXITED(status)) {
                    LOG(ERROR) << "Node with PID " << *it << " exited with status: " << WEXITSTATUS(status);
                } else if (WIFSIGNALED(status)) {
                    LOG(ERROR) << "Node with PID " << *it << " terminated by signal: " << WTERMSIG(status);
                }
                it = node_pids.erase(it);
            } else if (result < 0 && errno != ECHILD) {
                LOG(ERROR) << "Error waiting for child process " << *it << ": " << strerror(errno);
                it = node_pids.erase(it);
            } else {
                ++it;
            }
        }
        
        if (node_pids.empty()) {
            LOG(WARNING) << "All nodes have terminated. Exiting...";
            cleanup_and_exit(1);
        }
        
        // Sleep briefly to avoid busy waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    // Shutdown was requested
    LOG(INFO) << "Shutdown requested, cleaning up...";
    
    cleanup_and_exit(0);
}
