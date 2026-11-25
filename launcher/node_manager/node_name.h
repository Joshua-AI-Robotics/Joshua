#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <string>

#include "ros2/proto/node.pb.h"

namespace launcher {
inline std::string NodeExecutableName(ros2::node::NodeType type) {
  std::string name = ros2::node::NodeType_Name(type);
  std::transform(
      name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
  return name;
}

inline std::string RuntimeNodeName(ros2::node::NodeType type, uint32_t id) {
  const auto suffix = type == ros2::node::BOARD_PUBSUB ? "_" : "_node_";
  return NodeExecutableName(type) + suffix + std::to_string(id);
}
}  // namespace launcher
