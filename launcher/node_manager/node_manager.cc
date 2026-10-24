#include "launcher/node_manager/node_manager.h"

#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <utility>

#include "config/config_utils.h"
#include "config/validation.h"
#include "glog/logging.h"
#include "utils/joshua_runtime.h"
#include "utils/scope_exit.h"
#include "utils/status_macros.h"

extern char** environ;

namespace launcher {
namespace {
using SteadyClock = std::chrono::steady_clock;
constexpr int kTickMs = 10;
volatile sig_atomic_t stop_signal = 0;
NodeManager* active_manager = nullptr;

// Common environment variables.
constexpr auto kROS2NodeWrapper = "ros2_node_wrapper.sh";
constexpr auto kROS2 = "ros2";

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
std::optional<std::filesystem::path> ResolveNativeBinaryInBazelBin(const std::string& exec_name) {
  std::string self_exe = GetSelfExecutablePath();
  if (self_exe.empty()) return std::nullopt;
  std::filesystem::path exe_dir = std::filesystem::path(self_exe).parent_path();
  if (!exe_dir.has_parent_path()) return std::nullopt;
  std::filesystem::path bazel_bin_root = exe_dir.parent_path();
  std::filesystem::path candidate = bazel_bin_root / kROS2 / exec_name;
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

bool IsExecutablePathValid(const std::filesystem::path& candidate) {
  return std::filesystem::exists(candidate) && access(candidate.c_str(), X_OK) == 0;
}

bool IsExecutableAvailable(const std::string& exec_name) {
  if (auto native_bin = ResolveNativeBinaryInBazelBin(exec_name)) {
    if (IsExecutablePathValid(*native_bin)) {
      return true;
    }
  }

  // Packaged layout: check next to wrapper or under ros2/
  if (auto wrapper = ResolveRos2WrapperPath()) {
    std::filesystem::path wrapper_dir = wrapper->parent_path();
    std::filesystem::path candidate = wrapper_dir / exec_name;
    if (IsExecutablePathValid(candidate)) {
      return true;
    }
    std::filesystem::path candidate_sub = wrapper_dir / kROS2 / exec_name;
    if (IsExecutablePathValid(candidate_sub)) {
      return true;
    }
  }

  // Runfiles layout
  if (auto runfiles_root_opt = GetRunfilesRoot()) {
    const std::filesystem::path runfiles_root = *runfiles_root_opt;
    const char* workspace_dirs[] = {"_main", "__main__"};
    for (const char* ws : workspace_dirs) {
      std::filesystem::path candidate = runfiles_root / ws / kROS2 / exec_name;
      if (IsExecutablePathValid(candidate)) {
        return true;
      }
    }
  }

  return false;
}

std::string ResolveConfigPath(const std::string& path) {
  const auto absolute = std::filesystem::absolute(path);
  if (std::filesystem::exists(absolute)) return absolute.lexically_normal().string();
  const auto executable = GetSelfExecutablePath();
  if (!executable.empty()) {
    const auto packaged = std::filesystem::path(executable).parent_path() / path;
    if (std::filesystem::exists(packaged)) return packaged.lexically_normal().string();
  }
  return absolute.lexically_normal().string();
}

std::optional<std::filesystem::path> ResolveSimulationBinary() {
  std::string self = GetSelfExecutablePath();
  if (self.empty()) return std::nullopt;

  std::filesystem::path exe_dir = std::filesystem::path(self).parent_path();
  if (!exe_dir.has_parent_path()) return std::nullopt;

  std::filesystem::path bazel_bin_root = exe_dir.parent_path();
  std::filesystem::path candidate = bazel_bin_root / "simulation" / "simulation";
  if (std::filesystem::exists(candidate) && access(candidate.c_str(), X_OK) == 0) {
    return candidate;
  }

  if (auto runfiles = GetRunfilesRoot()) {
    for (const char* ws : {"_main", "__main__"}) {
      std::filesystem::path c = *runfiles / ws / "simulation" / "simulation";
      if (std::filesystem::exists(c) && access(c.c_str(), X_OK) == 0) {
        return c;
      }
    }
  }

  return std::nullopt;
}

void ExtractTopicsFromNode(const ros2::node::Node& node,
                           const uint32_t node_id,
                           std::vector<std::string>& publish_topics,
                           std::vector<std::string>& subscribe_topics) {
  if (node.id() == node_id) {
    for (const auto& pub : node.publishers()) {
      publish_topics.push_back(pub.topic());
    }
    for (const auto& sub : node.subscriptions()) {
      subscribe_topics.push_back(sub.topic());
    }
  }
}
void IgnoreSignal(int) {}

void ParentDied(int) {
  // The guardian never execs, so this handler survives for its whole lifetime.
  // Its group contains only this job and descendants that obey group ownership.
  kill(-getpid(), SIGKILL);
  _exit(1);
}

struct Event {
  int kind;  // 1 = application PID, 2 = application wait status.
  int value;
};

void WriteAll(int fd, const void* data, size_t size) {
  const auto* bytes = static_cast<const char*>(data);
  while (size) {
    const ssize_t count = write(fd, bytes, size);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return;
    bytes += count;
    size -= count;
  }
}

// Before forking the executable, discard other jobs' inherited control fds.
void CloseExcept(const std::array<int, 3>& keep, unsigned int limit) {
  unsigned int first = 3;
  for (int fd : keep) {
    if (static_cast<unsigned int>(fd) > first) {
#ifdef SYS_close_range
      if (syscall(SYS_close_range, first, fd - 1, 0) != 0)
#endif
        for (unsigned int next = first; next < static_cast<unsigned int>(fd); ++next) close(next);
    }
    first = fd + 1;
  }
#ifdef SYS_close_range
  if (syscall(SYS_close_range, first, ~0U, 0) == 0) return;
#endif
  for (unsigned int fd = first; fd < limit; ++fd) close(fd);
}

// After fork, this path uses only system calls and precomputed arguments.
[[noreturn]] void GuardProcess(pid_t owner,
                               const NodeManager::LaunchSpec& spec,
                               char* const* arguments,
                               char* const* environment,
                               int events,
                               int exec_error,
                               int ready,
                               const std::array<int, 3>& keep,
                               unsigned int fd_limit) {
  CloseExcept(keep, fd_limit);
  struct sigaction action{};
  sigemptyset(&action.sa_mask);
  action.sa_handler = ParentDied;
  if (setpgid(0, 0) < 0 || sigaction(SIGUSR2, &action, nullptr) < 0 ||
      prctl(PR_SET_PDEATHSIG, SIGUSR2) < 0) {
    const int error = errno;
    WriteAll(exec_error, &error, sizeof(error));
    _exit(1);
  }
  if (getppid() != owner) ParentDied(0);
  action.sa_handler = IgnoreSignal;
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
  action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &action, nullptr);
  sigset_t mask;
  sigemptyset(&mask);
  sigprocmask(SIG_SETMASK, &mask, nullptr);

  const pid_t guardian = getpid();
  const pid_t application = fork();
  if (application < 0) {
    const int error = errno;
    WriteAll(exec_error, &error, sizeof(error));
    _exit(1);
  }
  if (application == 0) {
    close(events);
    action.sa_handler = SIG_DFL;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGUSR2, &action, nullptr);
    sigaction(SIGPIPE, &action, nullptr);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != guardian ||
        (!spec.working_directory.empty() && chdir(spec.working_directory.c_str()) < 0) ||
        (spec.require_ready && fcntl(ready, F_SETFD, 0) < 0)) {
      const int error = errno ? errno : ECHILD;
      WriteAll(exec_error, &error, sizeof(error));
      _exit(1);
    }
    if (!spec.require_ready) close(ready);
    execve(spec.executable.c_str(), arguments, environment);
    const int error = errno;
    WriteAll(exec_error, &error, sizeof(error));
    _exit(1);
  }
  close(exec_error);
  close(ready);
  const Event started{1, application};
  WriteAll(events, &started, sizeof(started));
  int status;
  for (;;) {
    const pid_t result = waitpid(application, &status, 0);
    if (result < 0 && errno == EINTR) continue;
    if (result != application) _exit(1);
    const Event exited{2, status};
    WriteAll(events, &exited, sizeof(exited));
    close(events);
    // Keep the group identity reserved until the owner cleans up descendants.
    for (;;) pause();
  }
}

void CloseFd(int& fd) noexcept {
  if (fd >= 0) close(fd);
  fd = -1;
}

absl::Status CreatePipe(int& read_end, int& write_end) {
  int descriptors[2];
  if (pipe2(descriptors, O_CLOEXEC) < 0)
    return absl::ErrnoToStatus(errno, "Cannot create child pipe");
  read_end = descriptors[0];
  write_end = descriptors[1];
  for (int* end : {&read_end, &write_end}) {
    if (*end >= 3) continue;
    const int copy = fcntl(*end, F_DUPFD_CLOEXEC, 3);
    if (copy < 0) return absl::ErrnoToStatus(errno, "Cannot reserve child control fd");
    CloseFd(*end);
    *end = copy;
  }
  const int flags = fcntl(read_end, F_GETFL);
  if (flags < 0 || fcntl(read_end, F_SETFL, flags | O_NONBLOCK) < 0)
    return absl::ErrnoToStatus(errno, "Cannot configure child pipe");
  return absl::OkStatus();
}

void ConfigureRunfiles(NodeManager::LaunchSpec& spec, const std::filesystem::path& binary) {
  const auto own_runfiles = binary.parent_path() / (binary.filename().string() + ".runfiles");
  const auto runfiles = std::filesystem::exists(own_runfiles)
      ? std::optional<std::filesystem::path>(own_runfiles) : GetRunfilesRoot();
  spec.unset_environment = {"RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"};
  if (runfiles) {
    spec.environment = {{"RUNFILES_DIR", runfiles->string()}, {"TEST_SRCDIR", runfiles->string()}};
    const auto working_directory = *runfiles / "_main";
    if (std::filesystem::is_directory(working_directory))
      spec.working_directory = working_directory.string();
  } else {
    spec.unset_environment.insert(spec.unset_environment.end(), {"RUNFILES_DIR", "TEST_SRCDIR"});
  }
}
}  // namespace

NodeManager::NodeManager(const std::string& config_path)
    : owner_pid_(getpid()),
      config_path_(config_path.empty() ? std::string{} : ResolveConfigPath(config_path)) {
  if (active_manager && active_manager->owner_pid_ == owner_pid_)
    throw std::logic_error("Only one NodeManager may own a launcher process");
  if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0)
    throw std::runtime_error("Cannot enable managed-process subreaping");
  InstallSignalHandlers();
  active_manager = this;
}

NodeManager::~NodeManager() {
  if (owner_pid_ == getpid()) {
    joshua::RunCleanup("node manager shutdown", [&] { joshua::ThrowIfError(Shutdown()); });
    for (auto& node : nodes_) {
      joshua::RunCleanup("node manager final reap", [&] {
        if (node.status.guardian_pid <= 0 || node.status.reaped) return;
        if (!node.group_killed) kill(-node.status.process_group, SIGKILL);
        if (!node.guardian_reaped) kill(node.status.guardian_pid, SIGKILL);
        const auto deadline = SteadyClock::now() + std::chrono::seconds(1);
        while (!node.status.reaped && SteadyClock::now() < deadline) {
          ReapProcess(node).IgnoreError();
          if (!node.status.reaped) poll(nullptr, 0, kTickMs);
        }
      });
    }
    RestoreSignalHandlers();
    if (active_manager == this) active_manager = nullptr;
  }
  for (auto& node : nodes_) CloseChannels(node);
}

absl::Status NodeManager::CheckOwner() const {
  if (getpid() != owner_pid_)
    return absl::FailedPreconditionError("NodeManager belongs to another process");
  return absl::OkStatus();
}

void NodeManager::HandleSignal(int signal) noexcept { stop_signal = signal; }

void NodeManager::InstallSignalHandlers() {
  stop_signal = 0;
  struct sigaction action{};
  action.sa_handler = HandleSignal;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGINT, &action, &old_interrupt_) < 0)
    throw std::runtime_error("Cannot install SIGINT handler");
  if (sigaction(SIGTERM, &action, &old_terminate_) < 0) {
    sigaction(SIGINT, &old_interrupt_, nullptr);
    throw std::runtime_error("Cannot install SIGTERM handler");
  }
  signal_handlers_installed_ = true;
}

void NodeManager::RestoreSignalHandlers() noexcept {
  if (!signal_handlers_installed_) return;
  sigaction(SIGINT, &old_interrupt_, nullptr);
  sigaction(SIGTERM, &old_terminate_, nullptr);
  signal_handlers_installed_ = false;
}

bool NodeManager::ShutdownRequested() const noexcept {
  return stop_signal != 0 || joshua::ShutdownRequested();
}

absl::Status NodeManager::Initialize() {
  ABSL_RETURN_IF_ERROR(CheckOwner());
  if (graph_started_ || !nodes_.empty())
    return absl::FailedPreconditionError("Cannot reconfigure a started node graph");
  if (config_path_.empty()) return absl::InvalidArgumentError("No config path supplied");
  initialized_ = false;
  identified_nodes_.clear();
  ABSL_ASSIGN_OR_RETURN(config_, ::config::config_util::LoadConfig(config_path_));
  ABSL_RETURN_IF_ERROR(IdentifyNodeTypes());
  ABSL_RETURN_IF_ERROR(::config::ValidateConfig(config_));
  initialized_ = true;
  return absl::OkStatus();
}

absl::Status NodeManager::IdentifyNodeTypes() {
  auto register_node = [&](const ros2::node::Node& node) -> absl::Status {
    const auto [existing, inserted] = identified_nodes_.try_emplace(node.id(), node.node_type());
    if (!inserted && existing->second != node.node_type())
      return absl::InvalidArgumentError("Conflicting types for node ID " + std::to_string(node.id()));
    return absl::OkStatus();
  };
  for (const auto& action : config_.robot().actions().single_actions())
    ABSL_RETURN_IF_ERROR(register_node(action.node()));
  for (const auto& sensor : config_.robot().perceptions().single_perceptions())
    ABSL_RETURN_IF_ERROR(register_node(sensor.node()));
  for (const auto& model : config_.ai().models().single_models())
    ABSL_RETURN_IF_ERROR(register_node(model.node()));
  for (const auto& store : config_.ai().data_stores().single_data_stores())
    ABSL_RETURN_IF_ERROR(register_node(store.node()));
  for (const auto& trajectory : config_.robot().trajectories().single_trajectories())
    ABSL_RETURN_IF_ERROR(register_node(trajectory.node()));
  return absl::OkStatus();
}

absl::StatusOr<NodeManager::LaunchSpec> NodeManager::BuildNodeLaunchSpec(
    ros2::node::NodeType type, uint32_t id, const std::string& name) const {
  const std::string executable = NodeTypeToString(type);
  if (executable.empty() || !IsExecutableAvailable(executable))
    return absl::NotFoundError("No executable available for node " + name);
  LaunchSpec spec;
  spec.name = name;
  spec.node_id = id;
  spec.node_type = executable;
  if (const auto binary = ResolveNativeBinaryInBazelBin(executable);
      binary && IsExecutablePathValid(*binary)) {
    spec.executable = binary->string();
    spec.arguments = {name, std::to_string(id), config_path_};
    ConfigureRunfiles(spec, *binary);
  } else if (const auto wrapper = ResolveRos2WrapperPath();
             wrapper && IsExecutablePathValid(*wrapper)) {
    spec.executable = wrapper->string();
    spec.arguments = {executable, name, std::to_string(id), config_path_};
  } else {
    return absl::NotFoundError("Cannot resolve executable for node " + name);
  }
  // The isolated inference host has not adopted the readiness protocol yet.
  spec.require_ready = type != ros2::node::INFERENCE;
  spec.allow_completion = type == ros2::node::TRAJECTORY_PUBLISHER;
  return spec;
}

absl::StatusOr<NodeManager::LaunchSpec> NodeManager::BuildSimulationLaunchSpec() const {
  const auto executable = ResolveSimulationBinary();
  if (!executable) return absl::NotFoundError("Cannot locate simulation/simulation executable");
  LaunchSpec spec;
  spec.name = "simulation";
  spec.node_type = "simulation";
  spec.executable = executable->string();
  spec.arguments = {"--config", config_path_};
  spec.require_ready = false;
  spec.allow_completion = true;
  ConfigureRunfiles(spec, *executable);
  return spec;
}

absl::Status NodeManager::LaunchAllNodes() {
  ABSL_RETURN_IF_ERROR(CheckOwner());
  if (!initialized_) return absl::FailedPreconditionError("Initialize the node graph before launch");
  if (graph_started_) return absl::FailedPreconditionError("Node graph already started");
  std::vector<LaunchSpec> specs;
  for (const auto& [id, type] : identified_nodes_) {
    const auto name = NodeTypeToString(type) + "_node_" + std::to_string(id);
    ABSL_ASSIGN_OR_RETURN(auto spec, BuildNodeLaunchSpec(type, id, name));
    ABSL_RETURN_IF_ERROR(GetTopicsForNode(id, spec.publish_topics, spec.subscribe_topics));
    specs.push_back(std::move(spec));
  }
  if (specs.empty()) return absl::InvalidArgumentError("No nodes configured");
  graph_started_ = true;
  for (const auto& spec : specs) {
    const auto status = StartProcess(spec);
    if (!status.ok()) {
      Shutdown().IgnoreError();
      return status;
    }
  }
  const auto status = AwaitReady();
  if (!status.ok()) {
    Shutdown().IgnoreError();
    return status;
  }
  LOG(INFO) << "Node startup checks completed for " << specs.size() << " processes";
  return absl::OkStatus();
}

absl::Status NodeManager::LaunchSimulation() {
  ABSL_RETURN_IF_ERROR(CheckOwner());
  if (!initialized_) return absl::FailedPreconditionError("Initialize before launching simulation");
  auto spec = BuildSimulationLaunchSpec();
  auto status = spec.ok() ? StartProcess(*spec) : spec.status();
  if (status.ok()) status = AwaitReady();
  if (!status.ok()) Shutdown().IgnoreError();
  return status;
}

NodeManager::NodeRecord* NodeManager::FindNode(const std::string& name) {
  for (auto& node : nodes_) if (node.status.name == name) return &node;
  return nullptr;
}

absl::Status NodeManager::StartProcess(const LaunchSpec& spec) {
  ABSL_RETURN_IF_ERROR(CheckOwner());
  if (ShutdownRequested()) return absl::CancelledError("Process startup interrupted");
  if (spec.name.empty() || spec.executable.empty())
    return absl::InvalidArgumentError("Empty process spec");
  if (FindNode(spec.name)) return absl::AlreadyExistsError("Duplicate node name: " + spec.name);
  // Register the record before acquiring descriptors or forking.
  nodes_.emplace_back();
  auto& node = nodes_.back();
  node.spec = spec;
  node.status.name = spec.name;
  node.status.node_id = spec.node_id;
  node.status.node_type = spec.node_type;
  node.status.publish_topics = spec.publish_topics;
  node.status.subscribe_topics = spec.subscribe_topics;
  const auto status = SpawnProcess(node);
  if (!status.ok()) StartupFailed(node, status);
  return status;
}

absl::Status NodeManager::SpawnProcess(NodeRecord& node) {
  int event_write = -1, error_write = -1, ready_write = -1;
  joshua::ScopeExit close_writers([&] {
    CloseFd(event_write);
    CloseFd(error_write);
    CloseFd(ready_write);
  });
  ABSL_RETURN_IF_ERROR(CreatePipe(node.event_fd, event_write));
  ABSL_RETURN_IF_ERROR(CreatePipe(node.exec_fd, error_write));
  ABSL_RETURN_IF_ERROR(CreatePipe(node.ready_fd, ready_write));

  std::map<std::string, std::string> env;
  for (char** next = environ; *next; ++next) {
    const std::string entry(*next);
    const auto equal = entry.find('=');
    if (equal != std::string::npos) env[entry.substr(0, equal)] = entry.substr(equal + 1);
  }
  for (const auto& name : node.spec.unset_environment) env.erase(name);
  for (const auto& item : node.spec.environment) env[item.first] = item.second;
  env.erase("JOSHUA_READY_FD");
  if (node.spec.require_ready) env["JOSHUA_READY_FD"] = std::to_string(ready_write);
  std::vector<std::string> env_strings;
  for (const auto& item : env) env_strings.push_back(item.first + "=" + item.second);
  std::vector<char*> envp, argv;
  for (auto& entry : env_strings) envp.push_back(entry.data());
  envp.push_back(nullptr);
  argv.push_back(node.spec.executable.data());
  for (auto& arg : node.spec.arguments) argv.push_back(arg.data());
  argv.push_back(nullptr);
  std::array<int, 3> keep{event_write, error_write, ready_write};
  std::sort(keep.begin(), keep.end());
  struct rlimit limit{};
  if (getrlimit(RLIMIT_NOFILE, &limit) != 0) return absl::ErrnoToStatus(errno, "Descriptor limit");
  const pid_t owner = getpid();
  const pid_t guardian = fork();
  if (guardian < 0) return absl::ErrnoToStatus(errno, "Cannot fork node guardian");
  if (guardian == 0) {
    GuardProcess(owner, node.spec, argv.data(), envp.data(), event_write, error_write,
                 ready_write, keep, static_cast<unsigned int>(limit.rlim_cur));
  }
  node.status.guardian_pid = guardian;
  node.status.process_group = guardian;
  node.status.state = State::kStarting;
  // Establish the group from both sides before returning a controllable job.
  if (setpgid(guardian, guardian) < 0 && errno != EACCES && errno != ESRCH)
    return absl::ErrnoToStatus(errno, "Cannot establish node process group");
  return absl::OkStatus();
}

void NodeManager::StartupFailed(NodeRecord& node, const absl::Status& error) {
  FailProcess(node, error).IgnoreError();
  if (node.status.guardian_pid <= 0) {
    node.status.exited = true;
    node.status.reaped = true;
    CloseChannels(node);
  }
}

absl::Status NodeManager::PollProcess(NodeRecord& node) {
  if (!node.failure.ok() && !node.stopping) return node.failure;
  if (node.status.guardian_pid <= 0 || node.status.reaped) return absl::OkStatus();
  if (!node.status.executable_started && node.exec_fd >= 0) {
    int error;
    const ssize_t count = read(node.exec_fd, &error, sizeof(error));
    if (count == sizeof(error)) {
      CloseFd(node.exec_fd);
      return FailProcess(node, absl::ErrnoToStatus(error, "Cannot exec " + node.spec.name));
    }
    if (count == 0) {
      node.status.executable_started = true;
      if (!node.stopping && node.status.error.empty()) node.status.state = State::kRunning;
      CloseFd(node.exec_fd);
    } else if (count < 0 && errno != EAGAIN && errno != EINTR) {
      return FailProcess(node, absl::ErrnoToStatus(errno, "Exec status pipe"));
    }
  }
  for (;;) {
    Event event{};
    const ssize_t count = read(node.event_fd, &event, sizeof(event));
    if (count < 0 && (errno == EAGAIN || errno == EINTR)) break;
    if (count == 0) break;
    if (count != sizeof(event)) return FailProcess(node, absl::DataLossError("Incomplete process event"));
    if (event.kind == 1) {
      node.status.pid = event.value;
      std::fprintf(stderr, "Joshua: %s pid=%d guardian=%d group=%d\n", node.spec.name.c_str(),
                   node.status.pid, node.status.guardian_pid, node.status.process_group);
    }
    if (event.kind == 2) {
      node.status.exited = true;
      node.status.exit_code = WIFEXITED(event.value) ? WEXITSTATUS(event.value) : -1;
      node.status.termination_signal = WIFSIGNALED(event.value) ? WTERMSIG(event.value) : 0;
      if (node.status.error.empty())
        node.status.state = node.status.exit_code == 0 ? State::kExited : State::kFailed;
      node.status.expected_exit = node.status.error.empty() &&
          (node.stopping || (node.spec.allow_completion && node.status.exit_code == 0));
      if (!node.status.expected_exit)
        return FailProcess(node, absl::UnavailableError("Required node exited unexpectedly: " + node.spec.name +
            " exit=" + std::to_string(node.status.exit_code) +
            " signal=" + std::to_string(node.status.termination_signal)));
    }
  }
  if (!node.status.ready && node.ready_fd >= 0) {
    char ready;
    const ssize_t count = read(node.ready_fd, &ready, 1);
    if (count == 1 && ready == 'R') {
      node.status.ready = true;
      if (!node.stopping && !node.status.exited && node.status.error.empty())
        node.status.state = State::kReady;
      CloseFd(node.ready_fd);
    } else if (count == 0 && node.spec.require_ready && !node.stopping) {
      return FailProcess(node, absl::UnavailableError("Node closed readiness pipe before initialization: " + node.spec.name));
    } else if (count > 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) {
      return FailProcess(node, absl::DataLossError("Invalid node readiness notification: " + node.spec.name));
    }
    if (count == 0) CloseFd(node.ready_fd);
  }
  // WNOWAIT reserves the guardian's PID/PGID until group cleanup is complete.
  if (node.guardian_reaped) return absl::OkStatus();
  siginfo_t info{};
  if (waitid(P_PID, node.status.guardian_pid, &info, WEXITED | WNOHANG | WNOWAIT) < 0)
    return FailProcess(node, absl::ErrnoToStatus(errno, "Cannot observe node guardian"));
  if (info.si_pid && !node.stopping)
    return FailProcess(node, absl::UnavailableError("Node guardian exited unexpectedly: " + node.spec.name));
  return absl::OkStatus();
}

absl::Status NodeManager::SignalProcess(NodeRecord& node, int signal) {
  node.stopping = true;
  if (node.status.guardian_pid <= 0 || node.status.reaped || node.group_killed) return absl::OkStatus();
  if (signal == SIGKILL && !node.status.exited) node.status.forced_shutdown = true;
  if (!node.status.exited && node.status.error.empty()) node.status.state = State::kStopping;
  if (kill(-node.status.process_group, signal) < 0 && errno != ESRCH)
    return absl::ErrnoToStatus(errno, "Cannot signal node group " + node.spec.name);
  if (signal == SIGKILL) node.group_killed = true;
  return absl::OkStatus();
}

absl::Status NodeManager::ReapProcess(NodeRecord& node) {
  if (node.status.guardian_pid <= 0 || node.status.reaped) return absl::OkStatus();
  for (;;) {
    int status;
    // The launcher is a subreaper: adopted descendants are reaped only from
    // this owned group, never through waitpid(-1).
    const pid_t result = waitpid(-node.status.process_group, &status, WNOHANG);
    if (result > 0) {
      if (result == node.status.guardian_pid) node.guardian_reaped = true;
      continue;
    }
    if (result < 0 && errno == EINTR) continue;
    if (result < 0 && errno != ECHILD) return absl::ErrnoToStatus(errno, "Cannot reap node group");
    const bool group_gone = kill(-node.status.process_group, 0) < 0 && errno == ESRCH;
    node.status.reaped = node.guardian_reaped && result < 0 && group_gone;
    break;
  }
  if (node.status.reaped) {
    if (!node.status.exited) {
      node.status.exited = true;
      node.status.expected_exit = node.stopping && node.status.error.empty();
      node.status.state = node.status.error.empty() ? State::kExited : State::kFailed;
    }
  }
  if (node.status.reaped) CloseChannels(node);
  return absl::OkStatus();
}

bool NodeManager::StartupComplete(const NodeRecord& node) const {
  return node.status.pid > 0 && node.status.executable_started &&
         (!node.spec.require_ready || node.status.ready);
}

absl::Status NodeManager::FailProcess(NodeRecord& node, const absl::Status& error) {
  node.status.state = State::kFailed;
  if (node.status.error.empty()) node.status.error = error.ToString();
  if (node.failure.ok()) node.failure = error;
  return error;
}

void NodeManager::CloseChannels(NodeRecord& node) noexcept {
  CloseFd(node.event_fd);
  CloseFd(node.exec_fd);
  CloseFd(node.ready_fd);
}

absl::Status NodeManager::AwaitReady(std::chrono::milliseconds timeout) {
  ABSL_RETURN_IF_ERROR(CheckOwner());
  if (timeout.count() < 0) return absl::InvalidArgumentError("Negative readiness timeout");
  const auto deadline = SteadyClock::now() + timeout;
  for (;;) {
    if (ShutdownRequested()) return absl::CancelledError("Startup interrupted");
    ABSL_RETURN_IF_ERROR(PollNodes());
    bool all_started = true;
    for (const auto& node : nodes_) all_started &= StartupComplete(node);
    if (all_started) return absl::OkStatus();
    if (SteadyClock::now() >= deadline) {
      for (auto& node : nodes_) {
        if (!StartupComplete(node))
          StartupFailed(node, absl::DeadlineExceededError("Readiness timed out: " + node.status.name));
      }
      return absl::DeadlineExceededError("Node readiness timed out");
    }
    poll(nullptr, 0, kTickMs);
  }
}

absl::Status NodeManager::PollNodes() {
  ABSL_RETURN_IF_ERROR(CheckOwner());
  absl::Status result;
  for (auto& node : nodes_) {
    const auto status = PollProcess(node);
    if (result.ok() && !status.ok()) result = status;
  }
  return result;
}

bool NodeManager::HasLiveNodes() const {
  for (const auto& node : nodes_)
    if (node.status.guardian_pid > 0 && !node.status.exited && !node.status.reaped) return true;
  return false;
}

bool NodeManager::IsRunning(const std::string& name) const {
  for (const auto& node : nodes_) {
    if (node.status.name == name)
      return node.status.guardian_pid > 0 && !node.status.exited && !node.status.reaped;
  }
  return false;
}

absl::Status NodeManager::MonitorNodes(const std::string& completion_node) {
  ABSL_RETURN_IF_ERROR(CheckOwner());
  if (!completion_node.empty() && !FindNode(completion_node))
    return absl::NotFoundError("Unknown completion node: " + completion_node);
  while (!ShutdownRequested()) {
    const auto status = PollNodes();
    if (!status.ok()) {
      Shutdown().IgnoreError();
      return status;
    }
    if (!HasLiveNodes() || (!completion_node.empty() && !IsRunning(completion_node)))
      return Shutdown();
    poll(nullptr, 0, 20);
  }
  return Shutdown();
}

absl::Status NodeManager::Shutdown(int max_wait_ms) {
  if (max_wait_ms < 0) return absl::InvalidArgumentError("Negative shutdown grace period");
  ShutdownPolicy policy;
  policy.interrupt_grace = std::chrono::milliseconds(max_wait_ms * 3LL / 5);
  policy.terminate_grace = std::chrono::milliseconds(max_wait_ms) - policy.interrupt_grace;
  return Shutdown(policy);
}

absl::Status NodeManager::Shutdown(const ShutdownPolicy& policy) {
  ABSL_RETURN_IF_ERROR(CheckOwner());
  if (policy.interrupt_grace.count() < 0 || policy.terminate_grace.count() < 0 ||
      policy.kill_grace.count() < 0) return absl::InvalidArgumentError("Negative shutdown grace period");
  absl::Status result;
  auto remember = [&](const absl::Status& status) {
    if (result.ok() && !status.ok()) result = status;
  };
  auto signal = [&](int value) {
    for (auto& node : nodes_) remember(SignalProcess(node, value));
  };
  auto wait = [&](std::chrono::milliseconds duration, bool reaping) {
    const auto deadline = SteadyClock::now() + duration;
    do {
      bool complete = true;
      for (auto& node : nodes_) {
        remember(PollProcess(node));
        if (reaping) remember(ReapProcess(node));
        complete &= reaping ? node.status.reaped : node.status.exited;
      }
      if (complete) return true;
      poll(nullptr, 0, kTickMs);
    } while (SteadyClock::now() < deadline);
    return false;
  };
  signal(SIGINT);
  if (!wait(policy.interrupt_grace, false)) {
    signal(SIGTERM);
    wait(policy.terminate_grace, false);
  }
  // Guardians reserve group identities until the final descendant sweep.
  signal(SIGKILL);
  if (!wait(policy.kill_grace, true) && result.ok())
    result = absl::DeadlineExceededError("Node process groups have not all been reaped");
  return result;
}

std::vector<NodeManager::NodeStatus> NodeManager::GetNodeStatus() const {
  std::vector<NodeStatus> result;
  result.reserve(nodes_.size());
  for (const auto& node : nodes_) result.push_back(node.status);
  return result;
}

absl::Status NodeManager::GetTopicsForNode(
    uint32_t node_id, std::vector<std::string>& publish_topics,
    std::vector<std::string>& subscribe_topics) {
  if (identified_nodes_.count(node_id) == 0) {
    LOG(WARNING) << "Node " << node_id << " not found in the config.";
    return absl::Status(absl::StatusCode::kInvalidArgument, "Node not found in the config.");
  }

  for (const auto& single_perception : config_.robot().perceptions().single_perceptions()) {
    ExtractTopicsFromNode(single_perception.node(), node_id, publish_topics, subscribe_topics);
  }

  for (const auto& single_action : config_.robot().actions().single_actions()) {
    ExtractTopicsFromNode(single_action.node(), node_id, publish_topics, subscribe_topics);
  }

  for (const auto& single_model : config_.ai().models().single_models()) {
    ExtractTopicsFromNode(single_model.node(), node_id, publish_topics, subscribe_topics);
  }

  for (const auto& single_data_store : config_.ai().data_stores().single_data_stores()) {
    ExtractTopicsFromNode(single_data_store.node(), node_id, publish_topics, subscribe_topics);
  }

  for (const auto& single_trajectory : config_.robot().trajectories().single_trajectories()) {
    ExtractTopicsFromNode(single_trajectory.node(), node_id, publish_topics, subscribe_topics);
  }

  return absl::OkStatus();
}

}  // namespace launcher
