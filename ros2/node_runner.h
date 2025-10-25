#ifndef ROS2_NODE_RUNNER_H_
#define ROS2_NODE_RUNNER_H_

#include <csignal>
#include <memory>
#include <string>

#include "config/config_utils.h"
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
  rclcpp::init(argc, argv);

  // Ensure external termination results in teardown
  std::signal(SIGTERM, detail::sigterm_handler);

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

  rclcpp::spin(std::make_shared<NodeT>(node_name, node_id, config));
  rclcpp::shutdown();
  return 0;
}

}  // namespace ros2_utils

#endif  // ROS2_NODE_RUNNER_H_
