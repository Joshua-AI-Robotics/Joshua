#include "node_generator.h"

#include <glog/logging.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>

#include "config/config_utils.h"

namespace node_generator {

namespace {
// Common environment variables.
constexpr auto kROS2NodeWrapper = "ros2_node_wrapper.sh";
constexpr auto kROS2 = "ros2";

// Node types.
// Last Added: kLidarPublisher.
constexpr auto kCameraPublisher = "camera_publisher";
constexpr auto kEncoderPublisher = "encoder_publisher";
constexpr auto kActuatorSubscriber = "actuator_subscriber";
constexpr auto kOperationalLimitCalibration = "operational_limit_calibration";
constexpr auto kLidarPublisher = "lidar_publisher";

// Operation modes.
// Last Added: inference.
constexpr auto kTeleoperate = "teleoperate";
constexpr auto kInference = "inference";
constexpr auto kTraining = "training";
constexpr auto kCalibration = "calibration";
constexpr auto kTest = "test";

std::string NodeTypeToString(const ros2::node::NodeType& type) {
  if (type == ros2::node::NODE_INVALID) {
    return "";
  }
  std::string name = ros2::node::NodeType_Name(type);
  std::transform(
      name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
  return name;
}

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

}  // namespace

// Static member initialization
NodeGenerator* NodeGenerator::instance_ = nullptr;

NodeGenerator::NodeGenerator(const std::string& config_path) : shutdown_requested_(false) {
  instance_ = this;

  // Normalize config path so all children receive an absolute path
  // 1) If absolute and exists -> keep
  // 2) If relative and exists from CWD -> make absolute
  // 3) Fallback: try relative to the executable directory
  std::filesystem::path provided_path(config_path);
  std::filesystem::path resolved_path;

  if (provided_path.is_absolute() && std::filesystem::exists(provided_path)) {
    resolved_path = provided_path;
  } else {
    std::filesystem::path abs_from_cwd = std::filesystem::absolute(provided_path);
    if (std::filesystem::exists(abs_from_cwd)) {
      resolved_path = abs_from_cwd;
    } else {
      const std::string self_exe = GetSelfExecutablePath();
      if (!self_exe.empty()) {
        std::filesystem::path exe_dir = std::filesystem::path(self_exe).parent_path();
        std::filesystem::path from_exe_dir = exe_dir / provided_path;
        if (std::filesystem::exists(from_exe_dir)) {
          resolved_path = std::filesystem::absolute(from_exe_dir);
        }
      }
    }
  }

  if (!resolved_path.empty()) {
    config_path_ = resolved_path.lexically_normal().string();
  } else {
    // Fall back to the original value; LoadConfig will report a clear error.
    config_path_ = config_path;
  }
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
    auto result = config::config_util::LoadConfig(config_path_);

    if (!result.ok()) {
      return absl::Status(absl::StatusCode::kInvalidArgument, "Failed to load config");
    }

    config_ = result.value();
  } catch (const std::exception& e) {
    LOG(ERROR) << "Failed to load config: " << e.what();
    return absl::Status(absl::StatusCode::kInvalidArgument, "Failed to load config");
  }

  if (!IdentifyNodeTypes().ok()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Node types identification failed");
  }

  if (!CheckConfigIntegrity().ok()) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Config integrity check failed");
  }

  SetupSignalHandlers();

  LOG(INFO) << "NodeGenerator initialized successfully";

  return absl::OkStatus();
}

absl::Status NodeGenerator::IdentifyNodeTypes() {
  // Actions
  for (const auto& single_action : config_.robot().actions().single_actions()) {
    const uint32_t node_id = single_action.node().id();
    auto [it, inserted] = identified_nodes_.try_emplace(node_id, single_action.node().node_type());
    if (!inserted && it->second != single_action.node().node_type()) {
      LOG(ERROR) << "Node ID " << node_id << " already exists for node type "
                 << NodeTypeToString(it->second);
      return absl::Status(absl::StatusCode::kInvalidArgument, "Node ID conflict");
    }
  }

  // Perceptions
  for (const auto& single_perception : config_.robot().perceptions().single_perceptions()) {
    const uint32_t node_id = single_perception.node().id();
    auto [it, inserted] =
        identified_nodes_.try_emplace(node_id, single_perception.node().node_type());
    if (!inserted && it->second != single_perception.node().node_type()) {
      LOG(ERROR) << "Node ID " << node_id << " already exists for node type "
                 << NodeTypeToString(it->second);
      return absl::Status(absl::StatusCode::kInvalidArgument, "Node ID conflict");
    }
  }

  // Inference
  for (const auto& single_model :
       config_.ai().models().single_model()) {  // TODO: Update single_model() to plural.
    const uint32_t node_id = single_model.node().id();
    auto [it, inserted] = identified_nodes_.try_emplace(node_id, single_model.node().node_type());
    if (!inserted && it->second != single_model.node().node_type()) {
      LOG(ERROR) << "Node ID " << node_id << " already exists for node type "
                 << NodeTypeToString(it->second);
      return absl::Status(absl::StatusCode::kInvalidArgument, "Node ID conflict");
    }
  }

  // Data store.
  // TODO: Add here.

  // Calibration
  for (const auto& single_calibration : config_.calibration().single_calibrations()) {
    const uint32_t node_id = single_calibration.node().id();
    auto [it, inserted] =
        identified_nodes_.try_emplace(node_id, single_calibration.node().node_type());
    if (!inserted && it->second != single_calibration.node().node_type()) {
      LOG(ERROR) << "Node ID " << node_id << " already exists for node type "
                 << NodeTypeToString(it->second);
      return absl::Status(absl::StatusCode::kInvalidArgument, "Node ID conflict");
    }
  }

  return absl::OkStatus();
}

absl::Status NodeGenerator::CheckConfigIntegrity() {
  auto robot_config = config_.robot();
  std::map<std::string, uint32_t> port_to_node_id;

  // Helper lambda to extract serial port from a perception's config if it exists.
  auto get_serial_port = [](const auto& perception_details) -> std::string {
    if (perception_details.comm().comm_type() == robot::comm::CommType::SERIAL) {
      return perception_details.comm().serial_config().port();
    }
    return "";
  };

  // Check for serial port conflicts among all perception devices.
  // This ensures a single physical port is not managed by multiple node processes.
  for (const auto& single_perception : robot_config.perceptions().single_perceptions()) {
    uint32_t node_id = single_perception.node().id();
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
                     << "' is assigned to multiple node_ids (" << port_to_node_id[port_name]
                     << " and " << node_id
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
    const std::string node_name =
        NodeTypeToString(node_type) + std::string("_node_") + std::to_string(node_id);
    pid_t pid = LaunchNode(node_type, node_id, node_name);
    if (pid > 0) {
      std::vector<std::string> publish_topics;
      std::vector<std::string> subscribe_topics;
      if (!GetTopicsForNode(node_id, publish_topics, subscribe_topics).ok()) {
        LOG(ERROR) << "Failed to get topics for node " << node_id;
        return absl::Status(absl::StatusCode::kInvalidArgument, "Failed to get topics for node");
      }
      launched_nodes_.push_back(
          {NodeTypeToString(node_type), node_name, node_id, pid, publish_topics, subscribe_topics});
    }
  }

  LOG(INFO) << "All " << launched_nodes_.size() << " nodes launched successfully.";
  return !launched_nodes_.empty()
             ? absl::OkStatus()
             : absl::Status(absl::StatusCode::kInvalidArgument, "No nodes launched");
}

pid_t NodeGenerator::LaunchNode(const ros2::node::NodeType& node_type,
                                uint32_t node_id,
                                const std::string& node_name) {
  const std::string node_type_str = NodeTypeToString(node_type);
  if (node_type_str.empty()) {
    LOG(ERROR) << "Invalid node type for node " << node_id;
    return -1;
  }

  std::string exec_path;
  std::vector<std::string> argv_strings;
  std::vector<std::pair<std::string, std::string>> env_to_set;
  std::vector<std::string> env_to_unset;
  std::string work_dir;

  // 1) Try to execute the native binary with its own runfiles when available (Bazel run).
  if (auto native_bin = ResolveNativeBinaryInBazelBin(node_type_str)) {
    exec_path = native_bin->string();
    if (!std::filesystem::exists(exec_path)) {
      LOG(ERROR) << "Native binary not found at '" << exec_path << "' for node '" << node_type_str
                 << "'";
      return -1;
    }

    std::filesystem::path bin_path(exec_path);
    std::filesystem::path runfiles_dir =
        bin_path.parent_path() / (bin_path.filename().string() + ".runfiles");

    if (std::filesystem::exists(runfiles_dir)) {
      env_to_set.push_back({"RUNFILES_DIR", runfiles_dir.string()});
      env_to_set.push_back({"TEST_SRCDIR", runfiles_dir.string()});
      env_to_unset = {"RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"};

      std::filesystem::path wd = runfiles_dir / "_main";
      if (std::filesystem::exists(wd) && std::filesystem::is_directory(wd)) {
        work_dir = wd.string();
      }
    } else {
      env_to_unset = {"RUNFILES_DIR", "TEST_SRCDIR", "RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"};
    }

    argv_strings = {exec_path, node_name, std::to_string(node_id), config_path_};
  }
  // 2) Try to execute the generic wrapper from packaged tar layout.
  else if (auto wrapper = ResolveRos2WrapperPath()) {
    exec_path = wrapper->string();
    if (!std::filesystem::exists(exec_path)) {
      LOG(ERROR) << "Wrapper script not found at '" << exec_path << "'";
      return -1;
    }
    if (access(exec_path.c_str(), X_OK) != 0) {
      LOG(ERROR) << "Wrapper script not executable: '" << exec_path << "' (chmod +x)";
      return -1;
    }
    argv_strings = {exec_path, node_type_str, node_name, std::to_string(node_id), config_path_};
  } else {
    LOG(ERROR) << "Could not locate ROS2 wrapper, native binary, or launch for node type '"
               << node_type_str;
    return -1;
  }

  // Prepare argv pointers
  std::vector<char*> argv_ptrs;
  argv_ptrs.reserve(argv_strings.size() + 1);
  for (const auto& s : argv_strings) {
    argv_ptrs.push_back(const_cast<char*>(s.c_str()));
  }
  argv_ptrs.push_back(nullptr);

  pid_t pid = fork();

  if (pid == 0) {
    // Put child in its own process group so we can signal the whole group (including grandchildren)
    if (setpgid(0, 0) != 0) {
      // Failed to set process group. This is not critical.
    }

    for (const auto& [k, v] : env_to_set) {
      setenv(k.c_str(), v.c_str(), 1);
    }
    for (const auto& k : env_to_unset) {
      unsetenv(k.c_str());
    }

    if (!work_dir.empty()) {
      if (chdir(work_dir.c_str()) != 0) {
        // Failed to chdir. Proceeding anyway as it might still work.
      }
    }

    execv(exec_path.c_str(), argv_ptrs.data());

    const char* msg = "Failed to execute node process\n";
    write(STDERR_FILENO, msg, strlen(msg));
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

absl::Status NodeGenerator::GetTopicsForNode(const uint32_t node_id,
                                             std::vector<std::string>& publish_topics,
                                             std::vector<std::string>& subscribe_topics) {
  if (identified_nodes_.count(node_id) == 0) {
    LOG(WARNING) << "Node " << node_id << " not found in the config.";
    return absl::Status(absl::StatusCode::kInvalidArgument, "Node not found in the config.");
  }

  // Get publish topics from perceptions.
  for (const auto& single_perception : config_.robot().perceptions().single_perceptions()) {
    if (single_perception.node().id() == node_id) {
      publish_topics.push_back(single_perception.publish_topic());
    }
  }

  // Get subscribe topics from actions.
  for (const auto& single_action : config_.robot().actions().single_actions()) {
    if (single_action.node().id() == node_id) {
      subscribe_topics.push_back(single_action.subscribe_topic());
    }
  }

  // Get publish and subscribe topics for AI node (match SingleModel by node_id).
  if (config_.has_ai()) {
    const auto& models = config_.ai().models();
    for (const auto& single_model : models.single_model()) {
      if (single_model.node().id() == node_id) {
        for (const auto& pub : single_model.pubishers()) {
          publish_topics.push_back(pub.topic());
        }
        for (const auto& sub : single_model.subscriptions()) {
          subscribe_topics.push_back(sub.topic());
        }
      }
    }
  }

  // Get publish and subscribe topics for calibration node.
  for (const auto& single_calibration : config_.calibration().single_calibrations()) {
    if (single_calibration.node().id() == node_id) {
      for (const auto& sub_topic : single_calibration.subscribe_topics()) {
        subscribe_topics.push_back(sub_topic);
        publish_topics.push_back(sub_topic + "_operational_limit");
      }
    }
  }
  return absl::OkStatus();
}
}  // namespace node_generator
