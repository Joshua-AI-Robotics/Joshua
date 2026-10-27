#pragma once

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>

#include "config/config_utils.h"
#include "config/validation.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/board/factory/board_factory.h"
#include "robot/comm/factory/comm_factory.h"
#include "utils/joshua_runtime.h"
#include "utils/robot_time.h"
#include "utils/scope_exit.h"

namespace ros2_utils {

// argv: <binary> <node_name> <node_id> <config_path>.
// Direct calls and JOSHUA_MAIN share a single process exception boundary.
template <typename NodeT>
int RunNode(int argc, char* argv[], const char* logger_name) {
  return joshua::RunGuarded(logger_name, [&] {
    std::shared_ptr<NodeT> node;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor;
    bool ros_initialized = false;
    joshua::ScopeExit cleanup([&] {
      // Signal callbacks run through rclcpp's handler thread; the serial wait
      // cancellation flag is also set there, even if a callback is blocked.
      joshua::RequestShutdown();
      joshua::RunCleanup("ROS node removal", [&] {
        if (executor && node) executor->remove_node(node, false);
      });
      joshua::RunCleanup("ROS executor cleanup", [&] { executor.reset(); });
      joshua::RunCleanup("ROS node cleanup", [&] { node.reset(); });
      robot::board::BoardFactory::ShutdownAll();
      robot::comm::CommFactory::ShutdownAll();
      if (ros_initialized) joshua::RunCleanup("ROS shutdown", [] { rclcpp::shutdown(); });
    });

    rclcpp::init(argc, argv);
    ros_initialized = true;
    rclcpp::on_shutdown([] { joshua::RequestShutdown(); });
    if (argc < 4) throw std::invalid_argument("Usage: <binary> <node_name> <node_id> <config_path>");
    const std::string node_name = argv[1];
    size_t parsed = 0;
    const int node_id = std::stoi(argv[2], &parsed);
    if (parsed != std::string(argv[2]).size() || node_id < 0)
      throw std::invalid_argument("node_id must be a nonnegative integer");

    auto config_or = config::config_util::LoadConfig(argv[3]);
    joshua::ThrowIfError(config_or.status());
    const config::Config& config = *config_or;
    joshua::ThrowIfError(config::ValidateConfig(config));
    JoshuaInit(config);
    node = std::make_shared<NodeT>(node_name, node_id, config);
    executor = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    executor->add_node(node);
    if (joshua::ShutdownRequested()) return joshua::RuntimeFailed() ? 1 : 0;
    joshua::ThrowIfError(joshua::NotifyReady());
    // A finite executor wait also observes failures reported by worker threads.
    while (rclcpp::ok() && !joshua::ShutdownRequested())
      executor->spin_once(std::chrono::milliseconds(20));
    return joshua::RuntimeFailed() ? 1 : 0;
  });
}

}  // namespace ros2_utils
