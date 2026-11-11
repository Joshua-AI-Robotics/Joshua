#pragma once

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "config/config_utils.h"
#include "config/validation.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/board/factory/board_factory.h"
#include "robot/comm/factory/comm_factory.h"
#include "ros2/logging.h"
#include "utils/joshua_runtime.h"
#include "utils/robot_time.h"
#include "utils/scope_exit.h"

namespace ros2_utils {

namespace detail {
// Reuse ROS callback-group scheduling while guarding each executor worker.
class GuardedMultiThreadedExecutor : public rclcpp::executors::MultiThreadedExecutor {
 public:
  explicit GuardedMultiThreadedExecutor(size_t threads)
      : MultiThreadedExecutor(
            rclcpp::ExecutorOptions(), threads, false, std::chrono::milliseconds(20)) {}

  void spin() override {
    if (spinning.exchange(true)) throw std::runtime_error("Executor is already spinning");
    std::vector<std::thread> workers;
    joshua::ScopeExit finish([&] {
      joshua::RunCleanup("ROS executor cancellation", [&] { cancel(); });
      for (auto& worker : workers)
        if (worker.joinable()) worker.join();
      spinning.store(false);
    });
    workers.reserve(get_number_of_threads());
    for (size_t i = 0; i < get_number_of_threads(); ++i)
      workers.push_back(joshua::StartWorker("ROS executor", [this, i] { run(i); }));
    while (rclcpp::ok(context_) && spinning.load() && !joshua::ShutdownRequested())
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
};
}  // namespace detail

// argv: <binary> <node_name> <node_id> <config_path>.
// Direct calls and JOSHUA_MAIN share a single process exception boundary.
template <typename NodeT>
int RunNode(int argc, char* argv[], const char* logger_name, size_t executor_threads = 0) {
  return joshua::RunGuarded(logger_name, [&] {
    std::shared_ptr<NodeT> node;
    std::unique_ptr<rclcpp::Executor> executor;
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

    InitializeLogging(argv[0]);
    SetLogNodeName(logger_name);
    if (argc < 4)
      throw std::invalid_argument("Usage: <binary> <node_name> <node_id> <config_path>");
    const std::string node_name = argv[1];
    SetLogNodeName(node_name);
    size_t parsed = 0;
    const int node_id = std::stoi(argv[2], &parsed);
    if (parsed != std::string(argv[2]).size() || node_id < 0)
      throw std::invalid_argument("node_id must be a nonnegative integer");

    auto config_or = config::config_util::LoadConfig(argv[3]);
    joshua::ThrowIfError(config_or.status());
    const config::Config& config = *config_or;
    joshua::ThrowIfError(config::ValidateConfig(config));
    InitializeRosLogging(argc, argv, config.general().ros2_log_mode());
    ros_initialized = true;
    rclcpp::on_shutdown([] { joshua::RequestShutdown(); });
    JoshuaInit(config);
    node = std::make_shared<NodeT>(node_name, node_id, config);
    if (executor_threads)
      executor = std::make_unique<detail::GuardedMultiThreadedExecutor>(executor_threads);
    else
      executor = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    executor->add_node(node);
    if (joshua::ShutdownRequested()) return joshua::RuntimeFailed() ? 1 : 0;
    joshua::ThrowIfError(joshua::NotifyReady());
    // A finite executor wait also observes failures reported by worker threads.
    if (executor_threads)
      executor->spin();
    else
      while (rclcpp::ok() && !joshua::ShutdownRequested())
        executor->spin_once(std::chrono::milliseconds(20));
    return joshua::RuntimeFailed() ? 1 : 0;
  });
}

}  // namespace ros2_utils
