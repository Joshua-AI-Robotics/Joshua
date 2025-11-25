#pragma once

#include <sys/types.h>

#include <any>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "config/proto/config.pb.h"
#include "ros2/proto/node.pb.h"

namespace node_generator {

struct NodeInfo {
  std::string node_type;
  std::string node_name;
  uint32_t node_id;
  pid_t pid;
  std::vector<std::string> publish_topics;
  std::vector<std::string> subscribe_topics;
};

class NodeGenerator {
 public:
  explicit NodeGenerator(const std::string& config_path);
  ~NodeGenerator();

  absl::Status Initialize();
  absl::Status LaunchAllNodes();
  absl::Status MonitorNodes();
  absl::Status Shutdown(const int max_wait_ms = 5000);

  // Used for Joshua Control Panel to get the launched nodes.
  absl::Status GetLaunchedNodes(std::vector<NodeInfo>& nodes);

  size_t get_launched_node_count() const {
    return launched_nodes_.size();
  }
  bool has_nodes() const {
    return !launched_nodes_.empty();
  }

 private:
  absl::Status IdentifyNodeTypes();
  absl::Status CheckConfigIntegrity();

  pid_t LaunchNode(const ros2::node::NodeType& node_type,
                   uint32_t node_id,
                   const std::string& node_name);

  // Process management
  void SetupSignalHandlers();
  void CleanupAndExit(int exit_code);
  void MonitorChildProcesses();

  // Shutdown helpers
  void KillAllNodes(int signal);
  bool WaitForNodesToExit(int timeout_ms);

  absl::Status GetTopicsForNode(const uint32_t node_id,
                                std::vector<std::string>& publish_topics,
                                std::vector<std::string>& subscribe_topics);

  // Map from node_id to node type.
  absl::flat_hash_map<uint32_t, ros2::node::NodeType> identified_nodes_;

  std::string config_path_;
  config::Config config_;

  // Map from PID to NodeInfo for O(1) lookup and monitoring.
  absl::flat_hash_map<pid_t, NodeInfo> launched_nodes_;
  mutable std::mutex nodes_mutex_;
  std::atomic<bool> shutdown_requested_;

  static NodeGenerator* instance_;
  static void SignalHandler(int sig);
};

}  // namespace node_generator
