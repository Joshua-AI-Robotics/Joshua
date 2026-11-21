#include "tools/mcp/ros_source_binding.h"

#include <algorithm>
#include <cctype>
#include <regex>

#include "absl/status/status.h"
#include "config/config_validation_cli.h"

namespace joshua::mcp {
bool RosSourceBinding::operator==(const RosSourceBinding& b) const {
  return camera == b.camera && topic == b.topic && node == b.node && ns == b.ns &&
         domain == b.domain;
}
absl::StatusOr<RosSourceBinding> ResolveImageSource(int root_fd,
                                                    const std::string& path,
                                                    const std::string& source_id,
                                                    int domain) {
  if (source_id.rfind("sensor:", 0) != 0 || source_id.size() <= 7 || source_id.size() > 512 ||
      source_id.find('\0') != std::string::npos || domain < 0 || domain > 232)
    return absl::InvalidArgumentError("Invalid configured source ID or ROS domain.");
  std::string stage;
  auto config = ::config::LoadConfigForInspection(path, root_fd, &stage);
  if (!config.ok()) return config.status();
  const robot::perception::SinglePerception* selected = nullptr;
  for (const auto& sensor : config->robot().perceptions().single_perceptions()) {
    if (sensor.sensor_name() != source_id.substr(7)) continue;
    if (selected) return absl::InvalidArgumentError("Configured sensor identity is ambiguous.");
    selected = &sensor;
  }
  if (!selected || selected->sensor_type() != robot::perception::IMAGE)
    return absl::InvalidArgumentError("Select one configured IMAGE sensor.");
  const ros2::node::Publisher* publisher = nullptr;
  for (const auto& candidate : selected->node().publishers()) {
    if (candidate.ros2_data_type() != ros2::data_type::IMAGE) continue;
    if (publisher) return absl::InvalidArgumentError("Configured image topic is ambiguous.");
    publisher = &candidate;
  }
  if (!publisher) return absl::InvalidArgumentError("Configured sensor has no IMAGE topic.");
  auto topic = publisher->topic();
  if (!topic.empty() && topic.front() != '/') topic = "/" + topic;
  if (topic.size() > 256 || !std::regex_match(topic, std::regex("(/[A-Za-z_][A-Za-z_0-9]*)+")))
    return absl::InvalidArgumentError("Configured image topic must be a valid absolute ROS name.");
  // Cameras can share a node when each owns a distinct IMAGE topic. Reject
  // overlapping topic ownership instead of rejecting the supported node layout.
  for (const auto& sensor : config->robot().perceptions().single_perceptions()) {
    if (&sensor == selected || sensor.sensor_type() != robot::perception::IMAGE) continue;
    for (const auto& candidate : sensor.node().publishers()) {
      if (candidate.ros2_data_type() != ros2::data_type::IMAGE) continue;
      auto other_topic = candidate.topic();
      if (!other_topic.empty() && other_topic.front() != '/') other_topic = "/" + other_topic;
      if (other_topic == topic)
        return absl::InvalidArgumentError("Configured IMAGE topic has ambiguous sensor ownership.");
    }
  }
  auto node = ros2::node::NodeType_Name(selected->node().node_type());
  std::transform(
      node.begin(), node.end(), node.begin(), [](unsigned char c) { return std::tolower(c); });
  node += "_node_" + std::to_string(selected->node().id());
  if (node.size() > 128 || !std::regex_match(node, std::regex("[A-Za-z_][A-Za-z_0-9]*")))
    return absl::InvalidArgumentError("Configured ROS node name is invalid.");
  return RosSourceBinding{source_id, topic, node, "/", domain};
}
}  // namespace joshua::mcp
