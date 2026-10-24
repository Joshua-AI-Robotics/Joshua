#include "node_generator/node_generator.h"

#include <poll.h>

#include <algorithm>
#include <cctype>
#include <utility>

#include "config/config_utils.h"
#include "config/validation.h"
#include "glog/logging.h"
#include "node_generator/launch_spec.h"
#include "utils/joshua_runtime.h"
#include "utils/status_macros.h"

namespace node_generator {
namespace {
std::string NodeTypeToString(ros2::node::NodeType type) {
  auto name = ros2::node::NodeType_Name(type);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char value) { return std::tolower(value); });
  return name;
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
}  // namespace

NodeGenerator::NodeGenerator(const std::string& config_path)
    : config_path_(ResolveConfigPath(config_path)) {}

NodeGenerator::~NodeGenerator() {
  joshua::RunCleanup("node generator cleanup", [&] { joshua::ThrowIfError(Shutdown()); });
}

absl::Status NodeGenerator::Initialize() {
  if (started_) return absl::FailedPreconditionError("Cannot reconfigure a started node graph");
  initialized_ = false;
  identified_nodes_.clear();
  ABSL_ASSIGN_OR_RETURN(config_, config::config_util::LoadConfig(config_path_));
  ABSL_RETURN_IF_ERROR(IdentifyNodeTypes());
  ABSL_RETURN_IF_ERROR(config::ValidateConfig(config_));
  initialized_ = true;
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
  for (const auto& single_model : config_.ai().models().single_models()) {
    const uint32_t node_id = single_model.node().id();
    auto [it, inserted] = identified_nodes_.try_emplace(node_id, single_model.node().node_type());
    if (!inserted && it->second != single_model.node().node_type()) {
      LOG(ERROR) << "Node ID " << node_id << " already exists for node type "
                 << NodeTypeToString(it->second);
      return absl::Status(absl::StatusCode::kInvalidArgument, "Node ID conflict");
    }
  }

  // Data store.
  for (const auto& single_data_store : config_.ai().data_stores().single_data_stores()) {
    const uint32_t node_id = single_data_store.node().id();
    auto [it, inserted] =
        identified_nodes_.try_emplace(node_id, single_data_store.node().node_type());
    if (!inserted && it->second != single_data_store.node().node_type()) {
      LOG(ERROR) << "Node ID " << node_id << " already exists for node type "
                 << NodeTypeToString(it->second);
      return absl::Status(absl::StatusCode::kInvalidArgument, "Node ID conflict");
    }
  }

  // Trajectories
  for (const auto& single_trajectory : config_.robot().trajectories().single_trajectories()) {
    const uint32_t node_id = single_trajectory.node().id();
    auto [it, inserted] =
        identified_nodes_.try_emplace(node_id, single_trajectory.node().node_type());
    if (!inserted && it->second != single_trajectory.node().node_type()) {
      LOG(ERROR) << "Node ID " << node_id << " already exists for node type "
                 << NodeTypeToString(it->second);
      return absl::Status(absl::StatusCode::kInvalidArgument, "Node ID conflict");
    }
  }

  return absl::OkStatus();
}

absl::Status NodeGenerator::LaunchAllNodes() {
  if (!initialized_) return absl::FailedPreconditionError("Initialize the node graph before launch");
  if (started_) return absl::FailedPreconditionError("Node graph already started");
  nodes_.clear();
  std::vector<ProcessSpec> specs;
  // Resolve every executable before opening any child-owned hardware.
  for (const auto& [id, type] : identified_nodes_) {
    const std::string name = NodeTypeToString(type) + "_node_" + std::to_string(id);
    ABSL_ASSIGN_OR_RETURN(auto spec, BuildNodeProcessSpec(type, id, name, config_path_));
    specs.push_back(std::move(spec));
    NodeInfo info;
    info.node_id = id;
    info.node_name = name;
    info.node_type = NodeTypeToString(type);
    ABSL_RETURN_IF_ERROR(GetTopicsForNode(id, info.publish_topics, info.subscribe_topics));
    nodes_.push_back(std::move(info));
  }
  if (specs.empty()) return absl::InvalidArgumentError("No nodes configured");
  started_ = true;
  for (const auto& spec : specs) {
    const auto status = supervisor_.Start(spec);
    if (!status.ok()) {
      Shutdown().IgnoreError();
      return status;
    }
  }
  const auto status = supervisor_.AwaitReady();
  if (!status.ok()) {
    Shutdown().IgnoreError();
    return status;
  }
  LOG(INFO) << "Node startup checks completed for " << specs.size() << " processes";
  return absl::OkStatus();
}

absl::Status NodeGenerator::PollNodes() {
  return supervisor_.Poll();
}

absl::Status NodeGenerator::MonitorNodes() {
  while (!SupervisorShutdownRequested() && !joshua::ShutdownRequested()) {
    const auto status = PollNodes();
    if (!status.ok()) {
      Shutdown().IgnoreError();
      return status;
    }
    if (!has_nodes()) return Shutdown();
    poll(nullptr, 0, 20);
  }
  return Shutdown();
}

absl::Status NodeGenerator::Shutdown(int max_wait_ms) {
  if (max_wait_ms < 0) return absl::InvalidArgumentError("Negative shutdown grace period");
  ShutdownPolicy policy;
  policy.interrupt_grace = std::chrono::milliseconds(max_wait_ms * 3LL / 5);
  policy.terminate_grace = std::chrono::milliseconds(max_wait_ms) - policy.interrupt_grace;
  return supervisor_.Shutdown(policy);
}

absl::Status NodeGenerator::GetLaunchedNodes(std::vector<NodeInfo>& nodes) {
  nodes.clear();
  const auto states = supervisor_.Snapshots();
  for (const auto& state : states) {
    for (const auto& node : nodes_) {
      if (node.node_name != state.name) continue;
      auto info = node;
      info.pid = state.pid;
      info.process = state;
      nodes.push_back(std::move(info));
      break;
    }
  }
  return absl::OkStatus();
}

absl::Status NodeGenerator::GetTopicsForNode(const uint32_t node_id,
                                             std::vector<std::string>& publish_topics,
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
}  // namespace node_generator
