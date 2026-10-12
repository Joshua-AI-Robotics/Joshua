#pragma once

#include <csignal>
#include <memory>
#include <string>

#include "config/config_utils.h"
#include "config/ros2_network.h"
#include "config/validation.h"
#include "rclcpp/rclcpp.hpp"

namespace ros2_utils {

namespace detail {
inline void sigterm_handler(int) noexcept {
  // Translate SIGTERM into a clean ROS2 shutdown so destructors run
  rclcpp::shutdown();
}
}  // namespace detail

// Runs a standard ROS2 node main:
// argv: <binary> <node_name> <node_id> <config_path>
// Constructs NodeT(node_name, node_id, config) and spins it.
// logger_name is used for usage/error logging.
template <typename NodeT>
int RunNode(int argc, char* argv[], const char* logger_name) {
  if (argc < 4) {
    RCLCPP_ERROR(rclcpp::get_logger(logger_name),
                 "Usage: %s <node_name> <node_id> <config_path>",
                 logger_name);
    return 1;
  }

  const std::string node_name = argv[1];
  const int node_id = std::stoi(argv[2]);
  const std::string config_path = argv[3];

  auto result = config::config_util::LoadConfig(config_path);

  if (!result.ok()) {
    LOG(ERROR) << "Failed to load config: " << result.status().message();
    return 1;
  }

  config::Config config = result.value();
  const auto validation_status = config::ValidateConfig(config);
  if (!validation_status.ok()) {
    LOG(ERROR) << "Invalid config: " << validation_status;
    return 1;
  }

  auto network = config::Ros2NetworkEnvironment::Prepare(config.general().ros2_network());
  if (!network.ok()) {
    LOG(ERROR) << network.status();
    return 1;
  }
  const auto applied = (*network)->Apply();
  if (!applied.ok()) {
    LOG(ERROR) << applied;
    return 1;
  }
  rclcpp::init(argc, argv);
  std::signal(SIGTERM, detail::sigterm_handler);

  try {
    rclcpp::spin(std::make_shared<NodeT>(node_name, node_id, config));
  } catch (const std::exception& error) {
    LOG(ERROR) << "Node failed: " << error.what();
    rclcpp::shutdown();
    return 1;
  }
  rclcpp::shutdown();
  return 0;
}

}  // namespace ros2_utils
