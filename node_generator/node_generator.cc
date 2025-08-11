#include "node_generator.h"
#include "config/config_utils.h"
#include <glog/logging.h>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <spawn.h>
#include <unordered_map>
#include <unordered_set>

extern char** environ;

namespace node_generator {

namespace {
    // TODO: Make this configurable.
    constexpr auto kRepoRoot = "/home/hmoon/Projects/ProjectJoshua";
    
    constexpr auto kROS2Target = "ros2:";
    constexpr auto kCameraPublisher = "camera_publisher";
    constexpr auto kEncoderPublisher = "encoder_publisher";
    constexpr auto kActuatorSubscriber = "actuator_subscriber";
    constexpr auto kTeleoperate = "teleoperate";
    constexpr auto kInference = "inference";
    constexpr auto kTraining = "training";
    constexpr auto kCalibration = "calibration";
    constexpr auto kTest = "test";
    constexpr auto kMockInference = "mock_inference"; // TODO: Remove this.
    constexpr auto kMockInferencePy = "mock_inference_py";

    // Centralized mappings for easy future extension.
    
    // <perception_type, node_type>
    const std::unordered_map<robot::perception::PerceptionType, const char*> kPerceptionToNodeType = {
        {robot::perception::PerceptionType::CAMERA, kCameraPublisher},
        {robot::perception::PerceptionType::ENCODER, kEncoderPublisher},
    };

    // <action_type, node_type>
    const std::unordered_map<robot::action::ActionType, const char*> kActionToNodeType = {
        {robot::action::ActionType::ACTUATOR, kActuatorSubscriber},
    };

    // <operation_mode, node_type>
    const std::unordered_map<config::General::OperationMode, const char*> kOperationModeToNodeType = {
        {config::General::MODE_TELEOPERATE, kTeleoperate},
        {config::General::MODE_INFERENCE, kInference}, // Not yet implemented.
        {config::General::MODE_TRAINING,  kTraining}, // Not yet implemented.
        {config::General::MODE_CALIBRATION, kCalibration}, // Not yet implemented.
        {config::General::MODE_TEST, kTest},
        {config::General::MODE_MOCK_INFERENCE, kMockInference}, // TODO: Remove this.
        {config::General::MODE_MOCK_INFERENCE_PY, kMockInferencePy}, // TODO: Remove this.
    };
}

// Static member initialization
NodeGenerator* NodeGenerator::instance_ = nullptr;

NodeGenerator::NodeGenerator(const std::string& config_path) 
    : config_path_(config_path), 
      repo_root_(kRepoRoot),
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
    LOG(INFO) << "AI Policy Name: " << config_.ai().policy_name();
    LOG(INFO) << "Operation Mode: " << config_.general().operation_mode();

    // Change to repository root
    if (chdir(repo_root_.c_str()) != 0) {
        LOG(ERROR) << "Failed to change to repository root: " << repo_root_;
        return false;
    }
    
    IdentifyNodeTypes();
    if (!CheckConfigIntegrity()) {
        return false;
    }
    DetermineRequiredBuilds();
    SetupSignalHandlers();
    
    return true;
}

void NodeGenerator::IdentifyNodeTypes() {
    auto robot_config = config_.robot();
    
    // Perceptions -> add node type(s)
    for (const auto& single_perception : robot_config.perceptions().single_perceptions()) {
        const uint32_t node_id = single_perception.node_id();
        auto it = kPerceptionToNodeType.find(single_perception.perception_type());
        if (it != kPerceptionToNodeType.end()) {
            identified_nodes_[node_id] = it->second;
        }
    }
    
    // Actions -> add node type(s)
    for (const auto& single_action : robot_config.actions().single_actions()) {
        const uint32_t node_id = single_action.node_id();
        auto it = kActionToNodeType.find(single_action.action_type());
        if (it != kActionToNodeType.end()) {
            identified_nodes_[node_id] = it->second;
        }
    }

    // AI -> add node type
    const auto ai_config = config_.ai();
    auto it = kOperationModeToNodeType.find(config_.general().operation_mode());
    if (it != kOperationModeToNodeType.end()) {
        identified_nodes_[ai_config.node_id()] = it->second;
    }
}

bool NodeGenerator::CheckConfigIntegrity() {
    auto robot_config = config_.robot();
    std::map<std::string, uint32_t> port_to_node_id;

    // Helper lambda to extract serial port from a perception's config if it exists.
    auto get_serial_port = [](const auto& perception_details) -> std::string {
        if (perception_details.comm_type() == robot::comm_interface::CommType::SERIAL) {
            return perception_details.serial_config().port();
        }
        return "";
    };
    
    // Check for serial port conflicts among all perception devices.
    // This ensures a single physical port is not managed by multiple node processes.
    for (const auto& single_perception : robot_config.perceptions().single_perceptions()) {
        uint32_t node_id = single_perception.node_id();
        std::string port_name;
        
        if (single_perception.has_camera()) {
            port_name = get_serial_port(single_perception.camera());
        } else if (single_perception.has_encoder()) {
            port_name = get_serial_port(single_perception.encoder());
        }

        if (!port_name.empty()) {
            if (port_to_node_id.count(port_name)) {
                if (port_to_node_id[port_name] != node_id) {
                    LOG(ERROR) << "Configuration Integrity Failure: Serial port '" << port_name 
                                 << "' is assigned to multiple node_ids (" 
                                 << port_to_node_id[port_name] << " and " << node_id
                                 << "). This is not allowed as it will cause resource conflicts.";
                    return false;
                }
            } else {
                port_to_node_id[port_name] = node_id;
            }
        }
    }

    // TODO: Add more check here for the config.

    return true;
}

void NodeGenerator::DetermineRequiredBuilds() {
    // Each declared node type implies a build target `ros2:<node_type>`
    for (const auto& [_, node_type] : identified_nodes_) {
        required_builds_.insert(std::string(kROS2Target) + node_type);
    }
}

bool NodeGenerator::BuildRequiredTargets() {    
    std::atomic_bool dummy_stop{false};
    return BuildRequiredTargets(dummy_stop);
}

bool NodeGenerator::BuildRequiredTargets(std::atomic_bool& stop_flag) {
    for (const auto& build_target : required_builds_) {
        if (stop_flag.load()) {
            LOG(WARNING) << "Build stopped before starting target: " << build_target;
            return false;
        }

        pid_t pid = -1;
        const char* argv[] = {"bazel", "build", build_target.c_str(), nullptr};
        int spawn_rc = posix_spawnp(&pid, "bazel", nullptr, nullptr, const_cast<char* const*>(argv), environ);
        if (spawn_rc != 0) {
            LOG(ERROR) << "posix_spawnp failed for target " << build_target << ": " << strerror(spawn_rc);
            return false;
        }

        int status = 0;
        while (true) {
            pid_t res = waitpid(pid, &status, WNOHANG);
            if (res == pid) {
                if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    LOG(INFO) << "Built target: " << build_target;
                    break; // proceed to next target
                } else {
                    if (WIFEXITED(status)) {
                        LOG(ERROR) << "bazel build exited with status " << WEXITSTATUS(status)
                                   << " for target " << build_target;
                    } else if (WIFSIGNALED(status)) {
                        LOG(ERROR) << "bazel build killed by signal " << WTERMSIG(status)
                                   << " for target " << build_target;
                    }
                    return false;
                }
            } else if (res == 0) {
                if (stop_flag.load()) {
                    LOG(WARNING) << "Stop requested. Terminating build for target: " << build_target;
                    kill(pid, SIGTERM);
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    pid_t res2 = waitpid(pid, &status, WNOHANG);
                    if (res2 == 0) {
                        kill(pid, SIGKILL);
                        waitpid(pid, &status, 0);
                    }
                    return false;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            } else {
                if (errno == ECHILD) {
                    LOG(ERROR) << "waitpid returned ECHILD for build target: " << build_target;
                } else {
                    LOG(ERROR) << "waitpid error for build target " << build_target << ": " << strerror(errno);
                }
                return false;
            }
        }
    }

    return true;
}

bool NodeGenerator::LaunchAllNodes() {
    for (const auto& [node_id, node_type] : identified_nodes_) {
        const std::string node_name = node_type + std::string("_node_") + std::to_string(node_id);
        pid_t pid = LaunchNode(node_type, node_id, node_name);
        if (pid > 0) {
            std::vector<std::string> publish_topics;
            std::vector<std::string> subscribe_topics;
            GetTopicsForNode(node_id, publish_topics, subscribe_topics);
            launched_nodes_.push_back({node_type, node_name, node_id, pid, publish_topics, subscribe_topics});
        }
    }
    
    LOG(INFO) << "All " << launched_nodes_.size() << " nodes launched successfully.";
    return !launched_nodes_.empty();
}

pid_t NodeGenerator::LaunchNode(const std::string& node_type, uint32_t node_id,
                                const std::string& node_name) {
    pid_t pid = fork();

    if (pid == 0) {
        std::string binary_path = get_binary_path();
        std::string node_id_str = std::to_string(node_id);

        // Always use the wrapper script which handles AMENT setup and runfiles
        std::string exec_path = binary_path + "/" + node_type + "_wrapper.sh";

        execl(exec_path.c_str(),
              exec_path.c_str(),
              node_name.c_str(),
              node_id_str.c_str(),
              config_path_.c_str(),
              nullptr);

        LOG(ERROR) << "Failed to execute " << exec_path << ": " << strerror(errno);
        _exit(1);
    } else if (pid > 0) {
        LOG(INFO) << "Launched " << node_name << " with PID: " << pid;
        return pid;
    } else {
        LOG(ERROR) << "Failed to fork process for " << node_name << ": " << strerror(errno);
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

void NodeGenerator::GetLaunchedNodes(std::vector<NodeInfo>& nodes) {
    nodes = launched_nodes_;
}

void NodeGenerator::SetupSignalHandlers() {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
}

void NodeGenerator::CleanupAndExit(int exit_code) {
    Shutdown();
    exit(exit_code);
}

std::string NodeGenerator::get_binary_path() const {
    std::filesystem::path wrapper_script_path = std::filesystem::path(repo_root_) / "bazel-bin" / "ros2";
    return wrapper_script_path.string();
}

void NodeGenerator::SignalHandler(int sig) {
    if (instance_) {
        instance_->shutdown_requested_ = true;
    }
}

void NodeGenerator::GetTopicsForNode(const uint32_t node_id, std::vector<std::string>& publish_topics, std::vector<std::string>& subscribe_topics) {
    if(identified_nodes_.count(node_id) == 0) {
        LOG(WARNING) << "Node " << node_id << " not found in the config.";
        return;
    }

    // Get publish topics from perceptions.
    for(const auto& single_perception : config_.robot().perceptions().single_perceptions()) {
        if(single_perception.node_id() == node_id) {
            publish_topics.push_back(single_perception.publish_topic());
        }
    }

    // Get subscribe topics from actions.
    for(const auto& single_action : config_.robot().actions().single_actions()) {
        if(single_action.node_id() == node_id) {
            subscribe_topics.push_back(single_action.subscribe_topic());
        }
    }

    // Get publish and subscribe topics for AI node.
    if(config_.ai().node_id() == node_id) {
        for(const auto& pub_topic : config_.ai().publish_topics()) {
            publish_topics.push_back(pub_topic);
        }
        for(const auto& sub_topic : config_.ai().subscribe_topics()) {
            subscribe_topics.push_back(sub_topic);
        }
    }
}

} // namespace node_generator 
