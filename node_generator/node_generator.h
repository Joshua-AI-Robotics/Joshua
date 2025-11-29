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

// NodeGenerator manages the lifecycle of ROS2 nodes based on a configuration file.
// It handles node launching, monitoring, and shutdown.
class NodeGenerator {
 public:
  // Constructs a NodeGenerator with the given configuration file path.
  explicit NodeGenerator(const std::string& config_path);
  ~NodeGenerator();

  // Initializes the NodeGenerator by loading the config and verifying its integrity.
  // Returns an error if loading or verification fails.
  absl::Status Initialize();

  // Launches all nodes defined in the configuration.
  // Returns an error if no nodes could be launched.
  absl::Status LaunchAllNodes();

  // Monitors launched nodes for unexpected exits and handles process cleanup.
  // This method blocks until a shutdown signal is received or all nodes exit.
  absl::Status MonitorNodes();

  // Initiates a graceful shutdown of all managed nodes.
  // max_wait_ms specifies the maximum time to wait for nodes to exit after SIGINT/SIGTERM.
  absl::Status Shutdown(const int max_wait_ms = 5000);

  // Retrieves information about currently launched nodes.
  // Populates the provided vector with NodeInfo structures.
  absl::Status GetLaunchedNodes(std::vector<NodeInfo>& nodes);

  size_t get_launched_node_count() const {
    return launched_nodes_.size();
  }
  bool has_nodes() const {
    return !launched_nodes_.empty();
  }

 private:
  // Scans the configuration to identify all unique node IDs and their types.
  absl::Status IdentifyNodeTypes();

  // Checks the configuration for logical errors or conflicts (e.g., port conflicts).
  absl::Status CheckConfigIntegrity();

  // Launches a single node process.
  // Returns the PID of the launched process, or -1 on failure.
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

  // Retrieves publish and subscribe topics for a specific node ID from the config.
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
