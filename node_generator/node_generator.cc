#include "node_generator.h"
#include "config/config_utils.h"
#include <glog/logging.h>
#include <filesystem>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <chrono>
#include <thread>
#include <unordered_map>
#include <limits.h>
#include <cstdlib>
#include <mutex>

namespace node_generator {

namespace {
    // Common environment variables.
    constexpr auto kROS2NodeWrapper = "ros2_node_wrapper.sh";
    constexpr auto kROS2 = "ros2";

    // Common file names.
    constexpr auto kFixSymlinksScript = "fix_symlinks.sh";

    // Node types.
    // Last Added: kOperationalLimitCalibration.
    constexpr auto kCameraPublisher = "camera_publisher";
    constexpr auto kEncoderPublisher = "encoder_publisher";
    constexpr auto kActuatorSubscriber = "actuator_subscriber";
    constexpr auto kOperationalLimitCalibration = "operational_limit_calibration";

    // Operation modes.
    // Last Added: mock_inference_py.
    constexpr auto kTeleoperate = "teleoperate";
    constexpr auto kInference = "inference";
    constexpr auto kTraining = "training";
    constexpr auto kCalibration = "calibration";
    constexpr auto kTest = "test";
    constexpr auto kMockInference = "mock_inference"; // TODO: Remove this.
    constexpr auto kMockInferencePy = "mock_inference_py";
    
    // Centralized mappings for easy future extension.

    // <perception_type, node_type>
    // Last Added: kEncoderPublisher.
    const std::unordered_map<robot::perception::PerceptionType, const char*> kPerceptionToNodeType = {
        {robot::perception::PerceptionType::CAMERA, kCameraPublisher},
        {robot::perception::PerceptionType::ENCODER, kEncoderPublisher},
    };

    // <action_type, node_type>
    // Last Added: kActuatorSubscriber.
    const std::unordered_map<robot::action::ActionType, const char*> kActionToNodeType = {
        {robot::action::ActionType::ACTUATOR, kActuatorSubscriber},
    };

    // <operation_mode, node_type>
    // Last Added: kMockInferencePy.
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
    // Last Added: kOperationalLimitCalibration.
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

    // Try to find the native binary in Bazel's bin tree: <bazel-bin>/ros2/<name>
    std::optional<std::filesystem::path> ResolveNativeBinaryInBazelBin(const std::string& node_type) {
        std::string self_exe = GetSelfExecutablePath();
        if (self_exe.empty()) return std::nullopt;
        std::filesystem::path exe_dir = std::filesystem::path(self_exe).parent_path();
        if (!exe_dir.has_parent_path()) return std::nullopt;
        std::filesystem::path bazel_bin_root = exe_dir.parent_path();
        std::filesystem::path candidate = bazel_bin_root / kROS2 / node_type;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        return std::nullopt;
    }

    // Locate the generic wrapper script produced by //ros2:ros2_node_wrapper.sh
    std::optional<std::filesystem::path> ResolveRos2WrapperPath() {
        // 1) Next to the current executable (packaged tar layout)
        std::string self_exe = GetSelfExecutablePath();
        if (!self_exe.empty()) {
            std::filesystem::path self_dir = std::filesystem::path(self_exe).parent_path();
            std::filesystem::path candidate = self_dir / kROS2NodeWrapper;
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }
            // Also check under a ros2/ subdirectory in the same extracted tree
            std::filesystem::path candidate_sub = self_dir / kROS2 / kROS2NodeWrapper;
            if (std::filesystem::exists(candidate_sub)) {
                return candidate_sub;
            }
        }

        // 2) In this process' runfiles
        if (auto runfiles_root_opt = GetRunfilesRoot()) {
            const std::filesystem::path runfiles_root = *runfiles_root_opt;
            const char* workspace_dirs[] = {"_main", "__main__"};
            for (const char* ws : workspace_dirs) {
                std::filesystem::path ws_dir = runfiles_root / ws;
                std::filesystem::path candidate = ws_dir / kROS2 / kROS2NodeWrapper;
                if (std::filesystem::exists(candidate)) {
                    return candidate;
                }
            }
        }

        // 3) In bazel-bin/ros2
        if (!self_exe.empty()) {
            std::filesystem::path exe_dir = std::filesystem::path(self_exe).parent_path();
            if (exe_dir.has_parent_path()) {
                std::filesystem::path bazel_bin_root = exe_dir.parent_path();
                std::filesystem::path candidate = bazel_bin_root / kROS2 / kROS2NodeWrapper;
                if (std::filesystem::exists(candidate)) {
                    return candidate;
                }
            }
        }
        return std::nullopt;
    }

    void TryRunFixSymlinksOnce() {
        static std::once_flag run_once_flag;
        std::call_once(run_once_flag, []() {
            std::string self_exe = GetSelfExecutablePath();
            if (self_exe.empty()) return;
            std::filesystem::path self_dir = std::filesystem::path(self_exe).parent_path();
            std::filesystem::path fixer = self_dir / kFixSymlinksScript;
            if (!std::filesystem::exists(fixer)) return;

            pid_t pid = fork();
            if (pid == 0) {
                // Ensure the script runs from the directory that contains the *.runfiles folders
                if (chdir(self_dir.c_str()) != 0) {
                    _exit(127);
                }
                execl("/bin/bash", "/bin/bash", fixer.c_str(), nullptr);
                _exit(127);
            } else if (pid > 0) {
                int status;
                (void)waitpid(pid, &status, 0);
            }
        });
    }
}

// Static member initialization
NodeGenerator* NodeGenerator::instance_ = nullptr;

NodeGenerator::NodeGenerator(const std::string& config_path) 
    : config_path_(config_path), 
      shutdown_requested_(false) {
    instance_ = this;
}

NodeGenerator::~NodeGenerator() {
    if (has_nodes()) {
        auto res = Shutdown();
        if (!res.ok()) {
            LOG(ERROR) << "Failed to shutdown nodes";
        }
    }
    instance_ = nullptr;
}

absl::Status NodeGenerator::Initialize() {
    LOG(INFO) << "Initializing NodeGenerator with config: " << config_path_;
    
    try {
        config_ = config::config_util::LoadConfig(config_path_);
    } catch (const std::exception& e) {
        LOG(ERROR) << "Failed to load config: " << e.what();
        return absl::Status(absl::StatusCode::kInvalidArgument, "Failed to load config");
    }
    
    auto robot_config = config_.robot();
    LOG(INFO) << "Robot Name: " << robot_config.name();
    LOG(INFO) << "AI Policy Name: " << config_.ai().policy_name();
    LOG(INFO) << "Operation Mode: " << config_.general().operation_mode();
    
    if (!identify_node_types().ok()) {
        return absl::Status(absl::StatusCode::kInvalidArgument, "Node types identification failed");
    }

    if (!check_config_integrity().ok()) {
        return absl::Status(absl::StatusCode::kInvalidArgument, "Config integrity check failed");
    }

    SetupSignalHandlers();
    
    return absl::OkStatus();
}

absl::Status NodeGenerator::identify_node_types() {
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

    return absl::OkStatus();
}

absl::Status NodeGenerator::check_config_integrity() {
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
                    return absl::Status(absl::StatusCode::kInvalidArgument, "Serial port conflict");
                }
            } else {
                port_to_node_id[port_name] = node_id;
            }
        }
    }

    // TODO: Add more check here for the config.

    return absl::OkStatus();
}

absl::Status NodeGenerator::LaunchAllNodes() {
    for (const auto& [node_id, node_type] : identified_nodes_) {
        const std::string node_name = node_type + std::string("_node_") + std::to_string(node_id);
        pid_t pid = LaunchNode(node_type, node_id, node_name);
        if (pid > 0) {
            std::vector<std::string> publish_topics;
            std::vector<std::string> subscribe_topics;
            if (!GetTopicsForNode(node_id, publish_topics, subscribe_topics).ok()) {
                LOG(ERROR) << "Failed to get topics for node " << node_id;
                return absl::Status(absl::StatusCode::kInvalidArgument, "Failed to get topics for node");
            }
            launched_nodes_.push_back({node_type, node_name, node_id, pid, publish_topics, subscribe_topics});
        }
    }
    
    LOG(INFO) << "All " << launched_nodes_.size() << " nodes launched successfully.";
    return !launched_nodes_.empty() ? absl::OkStatus() : absl::Status(absl::StatusCode::kInvalidArgument, "No nodes launched");
}

pid_t NodeGenerator::LaunchNode(const std::string& node_type, uint32_t node_id,
                                const std::string& node_name) {
    pid_t pid = fork();

    if (pid == 0) {
        std::string node_id_str = std::to_string(node_id);
        TryRunFixSymlinksOnce();
        std::string exec_path;
        std::vector<const char*> argv_vec;
        
        // 1) Try to execute the native binary with its own runfiles when available (Bazel run).
        if (auto native_bin = ResolveNativeBinaryInBazelBin(node_type)) {
            exec_path = native_bin->string();
            if (!std::filesystem::exists(exec_path)) {
                LOG(ERROR) << "Native binary not found at '" << exec_path << "' for node '" << node_type << "'";
                _exit(1);
            }
            std::filesystem::path bin_path(exec_path);
            std::filesystem::path runfiles_dir = bin_path.parent_path() / (bin_path.filename().string() + ".runfiles");
            if (std::filesystem::exists(runfiles_dir)) {
                setenv("RUNFILES_DIR", runfiles_dir.c_str(), 1);
                setenv("TEST_SRCDIR", runfiles_dir.c_str(), 1);
                unsetenv("RUNFILES_MANIFEST_FILE");
                unsetenv("JAVA_RUNFILES");
                std::filesystem::path workdir = runfiles_dir / "_main";
                if (std::filesystem::exists(workdir) && std::filesystem::is_directory(workdir)) {
                    if (chdir(workdir.c_str()) != 0) {
                        LOG(WARNING) << "Failed to chdir to child runfiles workdir '" << workdir << "': " << strerror(errno);
                    }
                }
            } else {
                unsetenv("RUNFILES_DIR");
                unsetenv("TEST_SRCDIR");
                unsetenv("RUNFILES_MANIFEST_FILE");
                unsetenv("JAVA_RUNFILES");
            }
            argv_vec = {exec_path.c_str(), node_name.c_str(), node_id_str.c_str(), config_path_.c_str(), nullptr};
        }
        // 2) Try to execute the generic wrapper from packaged tar layout.
        else if (auto wrapper = ResolveRos2WrapperPath()) {
            exec_path = wrapper->string();
            if (!std::filesystem::exists(exec_path)) {
                LOG(ERROR) << "Wrapper script not found at '" << exec_path << "'";
                _exit(1);
            }
            if (access(exec_path.c_str(), X_OK) != 0) {
                LOG(ERROR) << "Wrapper script not executable: '" << exec_path << "' (chmod +x)";
                _exit(1);
            }
            argv_vec = {exec_path.c_str(), node_type.c_str(), node_name.c_str(), node_id_str.c_str(), config_path_.c_str(), nullptr};
        }
        else {
            LOG(ERROR) << "Could not locate ROS2 wrapper, native binary, or launch for node type '" << node_type;
            _exit(1);
        }

        // Put child in its own process group so we can signal the whole group (including grandchildren)
        if (setpgid(0, 0) != 0) {
            LOG(WARNING) << "Failed to set process group for " << node_name << ": " << strerror(errno) << ". This is not critical.";
        }

        execv(exec_path.c_str(), const_cast<char* const*>(argv_vec.data()));

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

absl::Status NodeGenerator::MonitorNodes() {
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
    return absl::OkStatus();
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

absl::Status NodeGenerator::Shutdown(const int max_wait_ms) {
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
    return absl::OkStatus();
}

absl::Status NodeGenerator::GetLaunchedNodes(std::vector<NodeInfo>& nodes) {
    nodes = launched_nodes_;
    return absl::OkStatus();
}

void NodeGenerator::SetupSignalHandlers() {
    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);
}

void NodeGenerator::CleanupAndExit(int exit_code) {
    auto res = Shutdown();
    if (!res.ok()) {
        LOG(ERROR) << "Failed to shutdown nodes";
    }
    exit(exit_code);
}

void NodeGenerator::SignalHandler(int sig) {
    if (instance_) {
        instance_->shutdown_requested_ = true;
    }
}

absl::Status NodeGenerator::GetTopicsForNode(const uint32_t node_id, std::vector<std::string>& publish_topics, std::vector<std::string>& subscribe_topics) {
    if(identified_nodes_.count(node_id) == 0) {
        LOG(WARNING) << "Node " << node_id << " not found in the config.";
        return absl::Status(absl::StatusCode::kInvalidArgument, "Node not found in the config.");
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
    return absl::OkStatus();
}
} // namespace node_generator 
