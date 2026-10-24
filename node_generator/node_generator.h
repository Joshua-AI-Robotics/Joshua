#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "config/proto/config.pb.h"
#include "node_generator/process_supervisor.h"
#include "ros2/proto/node.pb.h"

namespace node_generator {

struct NodeInfo {
  std::string node_type;
  std::string node_name;
  uint32_t node_id = 0;
  pid_t pid = -1;
  std::vector<std::string> publish_topics;
  std::vector<std::string> subscribe_topics;
  ProcessSnapshot process;
};

// Launcher-thread orchestration. ProcessSupervisor exclusively owns OS jobs.
class NodeGenerator {
 public:
  explicit NodeGenerator(const std::string& config_path);
  ~NodeGenerator();
  absl::Status Initialize();
  absl::Status LaunchAllNodes();
  absl::Status PollNodes();
  absl::Status MonitorNodes();
  absl::Status Shutdown(int max_wait_ms = 5000);
  // Includes retained exit records, not just currently live children.
  absl::Status GetLaunchedNodes(std::vector<NodeInfo>& nodes);
  size_t get_launched_node_count() const { return supervisor_.Snapshots().size(); }
  bool has_nodes() const { return supervisor_.HasLiveProcesses(); }

 private:
  absl::Status IdentifyNodeTypes();
  absl::Status GetTopicsForNode(uint32_t id,
                                std::vector<std::string>& publishers,
                                std::vector<std::string>& subscribers);
  std::string config_path_;
  config::Config config_;
  std::map<uint32_t, ros2::node::NodeType> identified_nodes_;
  std::vector<NodeInfo> nodes_;
  ProcessSupervisor supervisor_;
  bool initialized_ = false;
  bool started_ = false;
};

}  // namespace node_generator
