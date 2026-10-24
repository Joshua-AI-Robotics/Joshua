#pragma once

#include <csignal>
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "config/config_utils.h"
#include "config/validation.h"
#include "rclcpp/rclcpp.hpp"
#include "ros2/logging.h"
#include "utils/robot_time.h"

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
  InitializeLogging(argv[0]);
  SetLogNodeName(logger_name);

  // Ensure external termination results in teardown
  std::signal(SIGTERM, detail::sigterm_handler);

  if (argc < 4) {
    JOSHUA_LOG(ERROR) << "Usage: " << logger_name << " <node_name> <node_id> <config_path>";
    return 1;
  }

  try {
    const std::string node_name = argv[1];
    SetLogNodeName(node_name);
    const int node_id = std::stoi(argv[2]);
    const std::string config_path = argv[3];

    auto result = config::config_util::LoadConfig(config_path);

    if (!result.ok()) {
      JOSHUA_LOG(ERROR) << "Failed to load config: " << result.status();
      return 1;
    }

    config::Config config = result.value();
    const auto validation_status = config::ValidateConfig(config);
    if (!validation_status.ok()) {
      JOSHUA_LOG(ERROR) << "Invalid config: " << validation_status;
      return 1;
    }

    InitializeRosLogging(argc, argv, config.general().ros2_log_mode());
    // Install before node construction can create drivers, callbacks or workers.
    auto clock = joshua::MakeRobotClock(config.general().robot_clock());
    if (!clock.ok()) {
      JOSHUA_LOG(ERROR) << "Failed to create robot clock: " << clock.status();
      rclcpp::shutdown();
      return 1;
    }
    const std::string clock_source((*clock)->SourceName());
    const auto clock_status = joshua::SetGlobalRobotClock(std::move(*clock));
    if (!clock_status.ok()) {
      JOSHUA_LOG(ERROR) << "Failed to install robot clock: " << clock_status;
      rclcpp::shutdown();
      return 1;
    }
    JOSHUA_LOG(INFO) << "Robot clock initialized: " << clock_source;

    rclcpp::spin(std::make_shared<NodeT>(node_name, node_id, config));
    rclcpp::shutdown();
    return 0;
  } catch (const std::exception& error) {
    JOSHUA_LOG(ERROR) << "Node failed: " << error.what();
  } catch (...) {
    JOSHUA_LOG(ERROR) << "Node failed with an unknown exception";
  }
  if (rclcpp::ok()) rclcpp::shutdown();
  return 1;
}

}  // namespace ros2_utils
