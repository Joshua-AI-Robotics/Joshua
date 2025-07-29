#include "node_generator.h"
#include "config/config_utils.h"
#include <glog/logging.h>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>

namespace node_generator {

namespace {
    constexpr auto kROS2Target = "ros2:";
    constexpr auto kCameraPublisher = "camera_publisher";
    constexpr auto kEncoderPublisher = "encoder_publisher";
    constexpr auto kActuationSubscriber = "actuation_subscriber";
}

// Static member initialization
NodeGenerator* NodeGenerator::instance_ = nullptr;

NodeGenerator::NodeGenerator(const std::string& config_path) 
    : config_path_(config_path), 
      repo_root_("/home/hmoon/Projects/ProjectJoshua"),
      has_actuator_(false),
      shutdown_requested_(false) {
    instance_ = this;
}

NodeGenerator::~NodeGenerator() {
    if (HasNodes()) {
        Shutdown();
    }
    instance_ = nullptr;
}

bool NodeGenerator::Initialize() {
    LOG(INFO) << "Initializing NodeGenerator with config: " << config_path_;
    
    try {
        config_ = config::config_util::LoadConfig(config_path_);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to load config: " << e.what();
        return false;
    }
    
    auto robot_config = config_.robot();
    LOG(INFO) << "Robot Name: " << robot_config.name();
    LOG(INFO) << "Actuator Size: " << robot_config.actuations().single_actuation_size();
    LOG(INFO) << "Perception Size: " << robot_config.perceptions().single_perception_size();
    
    // Change to repository root
    if (chdir(repo_root_.c_str()) != 0) {
        LOG(ERROR) << "Failed to change to repository root: " << repo_root_;
        return false;
    }
    
    AnalyzeConfiguration();
    DetermineRequiredBuilds();
    SetupSignalHandlers();
    
    return true;
}

void NodeGenerator::AnalyzeConfiguration() {
    auto robot_config = config_.robot();
    
    // Group sensors by node_id
    for (const auto& single_perception : robot_config.perceptions().single_perception()) {
        if (single_perception.has_sensor()) {
            uint32_t node_id = single_perception.node_id();
            
            if (single_perception.sensor().sensor_type() == robot::perception::SensorType::CAMERA) {
                node_sensor_types_[node_id].insert("camera");
            } else if (single_perception.sensor().sensor_type() == robot::perception::SensorType::ENCODER) {
                node_sensor_types_[node_id].insert("encoder");
            }
        }
    }
    
    // TODO: Group actuators by node_id
    has_actuator_ = !robot_config.actuations().single_actuation().empty();
}

void NodeGenerator::DetermineRequiredBuilds() {
    // Determine required builds based on sensor types
    for (const auto& [node_id, sensor_types] : node_sensor_types_) {
        for (const auto& sensor_type : sensor_types) {
            if (sensor_type == "camera") {
                required_builds_.insert(std::string(kROS2Target) + kCameraPublisher);
            } else if (sensor_type == "encoder") {
                required_builds_.insert(std::string(kROS2Target) + kEncoderPublisher);
            }
        }
    }
    
    if (has_actuator_) {
        required_builds_.insert(std::string(kROS2Target) + kActuationSubscriber);
    }
}

bool NodeGenerator::BuildRequiredTargets() {    
    for (const auto& build_target : required_builds_) {
        std::string bazel_build_command = "bazel build " + build_target;
        int exit_code = system(bazel_build_command.c_str());
        if (exit_code != 0) {
            LOG(ERROR) << "Failed to build " << build_target << " with bazel build.";
            return false;
        }
    }
    
    return true;
}

bool NodeGenerator::LaunchAllNodes() {
    LOG(INFO) << "Launching nodes...";
    
    // Launch perception nodes - one per unique node_id
    for (const auto& [node_id, sensor_types] : node_sensor_types_) {
        std::string node_type = GetNodeTypePriority(sensor_types);
        if (node_type.empty()) {
            LOG(WARNING) << "Unknown sensor types for node_id " << node_id;
            continue;
        }
        
        std::string node_name = node_type + "_node_" + std::to_string(node_id);
        pid_t pid = LaunchPerceptionNode(node_type, node_id, node_name);
        
        if (pid > 0) {
            launched_nodes_.push_back({node_type, node_name, node_id, pid});
        }
    }
    
    // Launch actuation node if needed
    if (has_actuator_) {
        pid_t pid = LaunchActuationNode();
        if (pid > 0) {
            launched_nodes_.push_back({"actuation_subscriber", "actuation_subscriber", 0, pid});
        }
    }
    
    LOG(INFO) << "All " << launched_nodes_.size() << " nodes launched successfully.";
    return !launched_nodes_.empty();
}

pid_t NodeGenerator::LaunchPerceptionNode(const std::string& node_type, uint32_t node_id, 
                                         const std::string& node_name) {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        std::string binary_path = GetBinaryPath();
        std::string ament_path = binary_path + "/" + node_type + "_launch_ament_setup";
        setenv("AMENT_PREFIX_PATH", ament_path.c_str(), 1);
        
        std::string runfiles_dir = binary_path + "/" + node_type + ".runfiles/_main";
        if (chdir(runfiles_dir.c_str()) != 0) {
            LOG(ERROR) << "Failed to change to runfiles directory: " << runfiles_dir;
            _exit(1);
        }
        
        std::string binary_impl = binary_path + "/" + node_type + "_impl";
        std::string node_id_str = std::to_string(node_id);
        execl(binary_impl.c_str(), binary_impl.c_str(), config_path_.c_str(), node_id_str.c_str(), nullptr);
        
        LOG(ERROR) << "Failed to execute " << binary_impl << ": " << strerror(errno);
        _exit(1);
    } else if (pid > 0) {
        LOG(INFO) << "Launched " << node_name << " with PID: " << pid;
        return pid;
    } else {
        LOG(ERROR) << "Failed to fork process for " << node_name << ": " << strerror(errno);
        return -1;
    }
}

pid_t NodeGenerator::LaunchActuationNode() {
    pid_t pid = fork();
    
    if (pid == 0) {
        // Child process
        std::string binary_path = GetBinaryPath();
        std::string ament_path = binary_path + "/actuation_subscriber_launch_ament_setup";
        setenv("AMENT_PREFIX_PATH", ament_path.c_str(), 1);
        
        std::string runfiles_dir = binary_path + "/actuation_subscriber.runfiles/_main";
        if (chdir(runfiles_dir.c_str()) != 0) {
            LOG(ERROR) << "Failed to change to runfiles directory: " << runfiles_dir;
            _exit(1);
        }
        
        std::string binary_impl = binary_path + "/actuation_subscriber_impl";
        execl(binary_impl.c_str(), binary_impl.c_str(), config_path_.c_str(), nullptr);
        
        LOG(ERROR) << "Failed to execute " << binary_impl << ": " << strerror(errno);
        _exit(1);
    } else if (pid > 0) {
        LOG(INFO) << "Launched actuation_subscriber with PID: " << pid;
        return pid;
    } else {
        LOG(ERROR) << "Failed to fork process for actuation_subscriber: " << strerror(errno);
        return -1;
    }
}

void NodeGenerator::MonitorNodes() {
    LOG(INFO) << "Starting node monitoring...";
    
    while (!shutdown_requested_) {
        MonitorChildProcesses();
        
        if (launched_nodes_.empty()) {
            LOG(WARNING) << "All nodes have terminated. Exiting...";
            CleanupAndExit(1);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    LOG(INFO) << "Shutdown requested, cleaning up...";
    CleanupAndExit(0);
}

void NodeGenerator::MonitorChildProcesses() {
    for (auto it = launched_nodes_.begin(); it != launched_nodes_.end();) {
        int status;
        pid_t result = waitpid(it->pid, &status, WNOHANG);
        
        if (result > 0) {
            // Child process has terminated
            if (WIFEXITED(status)) {
                LOG(ERROR) << "Node " << it->node_name << " (PID " << it->pid 
                          << ") exited with status: " << WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                LOG(ERROR) << "Node " << it->node_name << " (PID " << it->pid 
                          << ") terminated by signal: " << WTERMSIG(status);
            }
            it = launched_nodes_.erase(it);
        } else if (result < 0 && errno != ECHILD) {
            LOG(ERROR) << "Error waiting for child process " << it->pid << ": " << strerror(errno);
            it = launched_nodes_.erase(it);
        } else {
            ++it;
        }
    }
}

void NodeGenerator::Shutdown() {
    LOG(INFO) << "Shutting down all nodes...";
    
    // Terminate all child processes
    for (const auto& node : launched_nodes_) {
        LOG(INFO) << "Terminating " << node.node_name << " with PID: " << node.pid;
        kill(node.pid, SIGTERM);
    }
    
    // Wait for processes to terminate gracefully
    for (const auto& node : launched_nodes_) {
        int status;
        if (waitpid(node.pid, &status, WNOHANG) == 0) {
            // Process still running, force kill after timeout
            sleep(2);
            kill(node.pid, SIGKILL);
            waitpid(node.pid, &status, 0);
        }
    }
    
    launched_nodes_.clear();
    LOG(INFO) << "All processes terminated successfully.";
}

void NodeGenerator::SetupSignalHandlers() {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
}

void NodeGenerator::CleanupAndExit(int exit_code) {
    Shutdown();
    exit(exit_code);
}

std::string NodeGenerator::GetBinaryPath() const {
    std::filesystem::path wrapper_script_path = std::filesystem::path(repo_root_) / "bazel-bin" / "ros2";
    return wrapper_script_path.string();
}

std::string NodeGenerator::GetNodeTypePriority(const std::set<std::string>& sensor_types) const {
    if (sensor_types.count("camera")) {
        return kCameraPublisher;
    } else if (sensor_types.count("encoder")) {
        return kEncoderPublisher;
    }
    return "";
}

void NodeGenerator::SignalHandler(int sig) {
    if (instance_) {
        instance_->shutdown_requested_ = true;
    }
}

} // namespace node_generator 