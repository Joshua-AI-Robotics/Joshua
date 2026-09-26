#pragma once

#include <functional>
#include <memory>
#include <string>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"

namespace ros2_utils {

using NodeFactory =
    std::function<std::shared_ptr<rclcpp::Node>(const std::string&, int, const config::Config&)>;

// Shared construction boundary for runners and in-process callers. Throws on
// invalid config before the factory can initialize any devices.
std::shared_ptr<rclcpp::Node> CreateValidatedNode(const std::string& node_name,
                                                  int node_id,
                                                  const config::Config& config,
                                                  const NodeFactory& factory);

int RunNode(int argc, char* argv[], const char* logger_name, const NodeFactory& factory);

// argv: <binary> <node_name> <node_id> <config_path>
// Only the concrete node construction needs to remain a template.
template <typename NodeT>
int RunNode(int argc, char* argv[], const char* logger_name) {
  return RunNode(
      argc, argv, logger_name, [](const std::string& name, int id, const config::Config& config) {
        return std::make_shared<NodeT>(name, id, config);
      });
}

}  // namespace ros2_utils
