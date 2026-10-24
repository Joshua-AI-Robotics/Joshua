#pragma once

#include <signal.h>
#include <sys/types.h>

#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/config.pb.h"
#include "ros2/proto/node.pb.h"

namespace launcher {

// One launcher-thread owner for configuration, node jobs, and process groups.
class NodeManager {
 public:
  enum class State { kSpawning, kStarting, kRunning, kReady, kStopping, kExited, kFailed };

  struct LaunchSpec {
    std::string name;
    std::string executable;
    std::vector<std::string> arguments;
    std::vector<std::pair<std::string, std::string>> environment;
    std::vector<std::string> unset_environment;
    std::string working_directory;
    bool require_ready = true;
    bool allow_completion = false;
    uint32_t node_id = 0;
    std::string node_type;
    std::vector<std::string> publish_topics;
    std::vector<std::string> subscribe_topics;
  };

  struct NodeStatus {
    std::string name;
    uint32_t node_id = 0;
    std::string node_type;
    std::vector<std::string> publish_topics;
    std::vector<std::string> subscribe_topics;
    pid_t pid = -1;
    pid_t guardian_pid = -1;
    pid_t process_group = -1;
    State state = State::kSpawning;
    bool executable_started = false;
    bool ready = false;
    bool exited = false;
    bool expected_exit = false;
    bool reaped = false;
    bool forced_shutdown = false;
    int exit_code = -1;
    int termination_signal = 0;
    std::string error;
  };

  struct ShutdownPolicy {
    std::chrono::milliseconds interrupt_grace{3000};
    std::chrono::milliseconds terminate_grace{2000};
    std::chrono::milliseconds kill_grace{1000};
  };

  explicit NodeManager(const std::string& config_path = {});
  ~NodeManager();
  NodeManager(const NodeManager&) = delete;
  NodeManager& operator=(const NodeManager&) = delete;

  absl::Status Initialize();
  absl::Status LaunchAllNodes();
  absl::Status LaunchSimulation();
  absl::Status StartProcess(const LaunchSpec& spec);
  absl::Status AwaitReady(std::chrono::milliseconds timeout = std::chrono::seconds(30));
  absl::Status PollNodes();
  // An optional finite job, such as simulation, controls when its graph stops.
  absl::Status MonitorNodes(const std::string& completion_node = {});
  absl::Status Shutdown(int max_wait_ms = 5000);
  absl::Status Shutdown(const ShutdownPolicy& policy);

  bool HasLiveNodes() const;
  bool IsRunning(const std::string& name) const;
  bool ShutdownRequested() const noexcept;
  std::vector<NodeStatus> GetNodeStatus() const;
  const ::config::Config& config() const { return config_; }
  const std::string& config_path() const { return config_path_; }

 private:
  // Handles belong to NodeManager, not to independent per-child owner objects.
  struct NodeRecord {
    LaunchSpec spec;
    NodeStatus status;
    absl::Status failure;
    int event_fd = -1;
    int exec_fd = -1;
    int ready_fd = -1;
    bool stopping = false;
    bool guardian_reaped = false;
    bool group_killed = false;
  };

  absl::Status CheckOwner() const;
  void InstallSignalHandlers();
  void RestoreSignalHandlers() noexcept;
  static void HandleSignal(int signal) noexcept;
  absl::Status IdentifyNodeTypes();
  absl::StatusOr<LaunchSpec> BuildNodeLaunchSpec(
      ros2::node::NodeType type, uint32_t id, const std::string& name) const;
  absl::StatusOr<LaunchSpec> BuildSimulationLaunchSpec() const;
  absl::Status GetTopicsForNode(uint32_t id,
                               std::vector<std::string>& publishers,
                               std::vector<std::string>& subscribers);
  NodeRecord* FindNode(const std::string& name);
  absl::Status SpawnProcess(NodeRecord& node);
  absl::Status PollProcess(NodeRecord& node);
  absl::Status SignalProcess(NodeRecord& node, int signal);
  absl::Status ReapProcess(NodeRecord& node);
  absl::Status FailProcess(NodeRecord& node, const absl::Status& error);
  void StartupFailed(NodeRecord& node, const absl::Status& error);
  bool StartupComplete(const NodeRecord& node) const;
  void CloseChannels(NodeRecord& node) noexcept;

  pid_t owner_pid_ = -1;
  struct sigaction old_interrupt_{};
  struct sigaction old_terminate_{};
  bool signal_handlers_installed_ = false;
  std::string config_path_;
  ::config::Config config_;
  std::map<uint32_t, ros2::node::NodeType> identified_nodes_;
  std::vector<NodeRecord> nodes_;
  bool initialized_ = false;
  bool graph_started_ = false;
};

}  // namespace launcher
