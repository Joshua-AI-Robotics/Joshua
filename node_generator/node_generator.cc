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
#include <limits.h>
#include <cstdlib>

extern char** environ;

namespace node_generator {

namespace {
    constexpr auto kROS2Target = "ros2:";

    // Node types.
    constexpr auto kCameraPublisher = "camera_publisher";
    constexpr auto kEncoderPublisher = "encoder_publisher";
    constexpr auto kActuatorSubscriber = "actuator_subscriber";
    constexpr auto kOperationalLimitCalibration = "operational_limit_calibration";

    // Operation modes.
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
        {config::General::MODE_CALIBRATION, kCalibration},
        {config::General::MODE_TEST, kTest},
        {config::General::MODE_MOCK_INFERENCE, kMockInference}, // TODO: Remove this.
        {config::General::MODE_MOCK_INFERENCE_PY, kMockInferencePy}, // TODO: Remove this.
    };

    // <calibration_mode, node_type>
    const std::unordered_map<config::CalibrationMode, const char*> kCalibrationModeToNodeType = {
        {config::CalibrationMode::CALIBRATION_MODE_OPERATIONAL_LIMIT, kOperationalLimitCalibration},
    };
    
    // Resolve current executable absolute path via /proc/self/exe
    std::string GetSelfExecutablePath() {
        char buffer[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
        if (len == -1) {
            return "";
        }
        buffer[len] = '\0';
        return std::string(buffer);
    }

    // Try to determine the Bazel runfiles root directory for this process
    std::optional<std::filesystem::path> GetRunfilesRoot() {
        const char* env_runfiles = std::getenv("RUNFILES_DIR");
        if (env_runfiles && std::filesystem::exists(env_runfiles)) {
            return std::filesystem::path(env_runfiles);
        }

        // Fallback: <self_exe>.runfiles
        std::string self_exe = GetSelfExecutablePath();
        if (!self_exe.empty()) {
            std::filesystem::path candidate = std::filesystem::path(self_exe + ".runfiles");
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    // Locate the ROS2 launch script for a given node type within runfiles.
    // Primary layout: <runfiles>/_main/ros2/<name>_launch, fallback: <runfiles>/__main__/ros2/<name>_launch
    std::optional<std::filesystem::path> ResolveLaunchPath(const std::string& node_type, std::filesystem::path* out_workspace_dir) {
        auto runfiles_root_opt = GetRunfilesRoot();
        const std::string launch_name = node_type + std::string("_launch");
        if (runfiles_root_opt) {
            const std::filesystem::path runfiles_root = *runfiles_root_opt;
            const char* workspace_dirs[] = {"_main", "__main__"};
            for (const char* ws : workspace_dirs) {
                std::filesystem::path ws_dir = runfiles_root / ws;
                std::filesystem::path candidate = ws_dir / "ros2" / launch_name;
                if (std::filesystem::exists(candidate)) {
                    if (out_workspace_dir) {
                        *out_workspace_dir = ws_dir;
                    }
                    return candidate;
                }
            }
        }

        // As a last resort, try locating next to bazel-bin of this binary: <exe_dir>/../../ros2/<name>_launch
        std::string self_exe = GetSelfExecutablePath();
        if (!self_exe.empty()) {
            std::filesystem::path exe_dir = std::filesystem::path(self_exe).parent_path();
            // Heuristic: node_generator/joshua_main => go up two levels to reach bazel-bin root
            std::filesystem::path candidate = exe_dir;
            if (candidate.has_parent_path()) candidate = candidate.parent_path();
            if (candidate.has_parent_path()) candidate = candidate.parent_path();
            candidate = candidate / "ros2" / launch_name;
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
        }
        return std::nullopt;
    }

    // Try to find the wrapper in Bazel's bin tree: <bazel-bin>/ros2/<name>_wrapper.sh
    std::optional<std::filesystem::path> ResolveWrapperInBazelBin(const std::string& node_type) {
        std::string self_exe = GetSelfExecutablePath();
        if (self_exe.empty()) return std::nullopt;
        std::filesystem::path exe_dir = std::filesystem::path(self_exe).parent_path();
        if (!exe_dir.has_parent_path()) return std::nullopt;
        std::filesystem::path bazel_bin_root = exe_dir.parent_path();
        std::filesystem::path candidate = bazel_bin_root / "ros2" / (node_type + std::string("_wrapper.sh"));
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        return std::nullopt;
    }
}

// Static member initialization
NodeGenerator* NodeGenerator::instance_ = nullptr;

NodeGenerator::NodeGenerator(const std::string& config_path) 
    : config_path_(config_path), 
      repo_root_(DetermineRepoRoot()),
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

    // Do NOT change the process-wide working directory here; use absolute paths and
    // run child processes with their own working directory where needed.
    
    IdentifyNodeTypes();
    if (!CheckConfigIntegrity()) {
        return false;
    }

    SetupSignalHandlers();
    
    return true;
}

void NodeGenerator::IdentifyNodeTypes() {
    // Actions -> add node type(s)
    for (const auto& single_action : config_.robot().actions().single_actions()) {
        const uint32_t node_id = single_action.node_id();
        auto it = kActionToNodeType.find(single_action.action_type());
        if (it != kActionToNodeType.end()) {
            identified_nodes_[node_id] = it->second;
        }
    }

    // Perceptions -> add node type(s)
    for (const auto& single_perception : config_.robot().perceptions().single_perceptions()) {
        const uint32_t node_id = single_perception.node_id();
        auto it = kPerceptionToNodeType.find(single_perception.perception_type());
        if (it != kPerceptionToNodeType.end()) {
            identified_nodes_[node_id] = it->second;
        }
    }    

    // AI -> add node type
    if(config_.has_ai()) {
        auto it = kOperationModeToNodeType.find(config_.general().operation_mode());
        if (it != kOperationModeToNodeType.end()) {
            identified_nodes_[config_.ai().node_id()] = it->second;
        }
    }

    // Calibration -> add node type
    if(config_.has_calibration()) {
        auto it = kCalibrationModeToNodeType.find(config_.calibration().calibration_mode());
        if (it != kCalibrationModeToNodeType.end()) {
            identified_nodes_[config_.calibration().node_id()] = it->second;
        }
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
        std::string node_id_str = std::to_string(node_id);

        // Prefer executing the wrapper from bazel-bin so its sibling .runfiles is available
        std::string exec_path;
        if (auto wrapper_bin = ResolveWrapperInBazelBin(node_type)) {
            exec_path = wrapper_bin->string();
        } else {
            // Fallback: run the launch script from runfiles by chdir-ing into the workspace dir
            std::filesystem::path workspace_dir;
            auto launch_path_opt = ResolveLaunchPath(node_type, &workspace_dir);
            if (!launch_path_opt) {
                LOG(ERROR) << "Could not locate ROS2 wrapper or launch for node type '" << node_type << "'. Ensure '//ros2:ros2_executables' is built.";
                _exit(1);
            }
            if (!workspace_dir.empty()) {
                if (chdir(workspace_dir.c_str()) != 0) {
                    LOG(WARNING) << "Failed to chdir to runfiles workspace '" << workspace_dir << "': " << strerror(errno);
                }
            }
            exec_path = launch_path_opt->string();
        }

        // Put child in its own process group so we can signal the whole group (including grandchildren)
        if (setpgid(0, 0) != 0) {
            LOG(WARNING) << "Failed to set process group for " << node_name << ": " << strerror(errno) << ". This is not critical.";
        }

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

void NodeGenerator::Shutdown(const int max_wait_ms) {
    LOG(INFO) << "Shutting down all nodes...";
    
    // Ask all child processes to shutdown gracefully (SIGINT preferred by ROS2)
    for (const auto& node : launched_nodes_) {
        if (node.pid <= 0) continue;
        LOG(INFO) << "Requesting shutdown (SIGINT) for " << node.node_name << " PID: " << node.pid;
        // Signal the entire process group (negative PID)
        kill(-node.pid, SIGINT);
    }
    
    // Split the total wait across SIGINT and SIGTERM phases
    const int poll_interval_ms = 100;
    const int int_wait_ms = static_cast<int>(max_wait_ms * 0.6);
    const int term_wait_ms = max_wait_ms - int_wait_ms;

    auto wait_until = [&](int timeout_ms) {
        int waited_ms = 0;
        while (waited_ms < timeout_ms) {
            bool any_running = false;
            for (const auto& node : launched_nodes_) {
                if (node.pid <= 0) continue;
                int status;
                pid_t res = waitpid(node.pid, &status, WNOHANG);
                if (res == 0) {
                    any_running = true;
                }
            }
            if (!any_running) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms));
            waited_ms += poll_interval_ms;
        }
        return false;
    };

    // Wait for SIGINT grace
    (void)wait_until(int_wait_ms);

    // Send SIGTERM to any remaining
    for (const auto& node : launched_nodes_) {
        int status;
        pid_t res = waitpid(node.pid, &status, WNOHANG);
        if (res == 0) {
            if (node.pid > 0) {
                LOG(WARNING) << node.node_name << " still running. Sending SIGTERM to group.";
                kill(-node.pid, SIGTERM);
            }
        }
    }

    // Wait for SIGTERM grace
    (void)wait_until(term_wait_ms);

    // Force kill any remaining
    for (const auto& node : launched_nodes_) {
        int status;
        pid_t res = waitpid(node.pid, &status, WNOHANG);
        if (res == 0) {
            if (node.pid > 0) {
                LOG(WARNING) << node.node_name << " did not exit in time. Sending SIGKILL to group.";
                kill(-node.pid, SIGKILL);
                waitpid(node.pid, &status, 0);
            }
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

std::string NodeGenerator::DetermineRepoRoot() const {
    // First, check if BUILD_WORKSPACE_DIRECTORY is set (when running via 'bazel run')
    const char* workspace_dir = std::getenv("BUILD_WORKSPACE_DIRECTORY");
    if (workspace_dir && std::filesystem::exists(workspace_dir)) {
        LOG(INFO) << "Repository root detected from BUILD_WORKSPACE_DIRECTORY: " << workspace_dir;
        return std::string(workspace_dir);
    }
    
    // Otherwise, traverse up from current directory to find repository root
    std::filesystem::path current_path = std::filesystem::current_path();
    const std::vector<std::string> repo_indicators = {
        "MODULE.bazel",
        "WORKSPACE", 
        "BUILD",
        ".git"
    };
    
    while (current_path != current_path.root_path()) {
        bool found_indicator = false;
        for (const auto& indicator : repo_indicators) {
            std::filesystem::path indicator_path = current_path / indicator;
            if (std::filesystem::exists(indicator_path)) {
                found_indicator = true;
                break;
            }
        }
        
        if (found_indicator) {
            LOG(INFO) << "Repository root detected at: " << current_path.string();
            return current_path.string();
        }
        
        current_path = current_path.parent_path();
    }
    
    // Fallback to current working directory if no indicators found
    std::string fallback = std::filesystem::current_path().string();
    LOG(WARNING) << "Could not detect repository root, using current directory: " << fallback;
    return fallback;
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

    // Get publish and subscribe topics for calibration node.
    if(config_.calibration().node_id() == node_id) {
        for(const auto& sub_topic : config_.calibration().subscribe_topics()) {
            subscribe_topics.push_back(sub_topic);
            publish_topics.push_back(sub_topic + "_operational_limit");
        }
    }
}

} // namespace node_generator 
