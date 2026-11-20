#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/action/interfaces/control_gate.h"
#include "std_srvs/srv/trigger.hpp"

namespace ros2_utils {

class ActuatorCommands {
 public:
  using ShutdownMode = robot::action::ControlGate::ShutdownMode;
  ActuatorCommands(rclcpp::Node& node,
                   int node_id,
                   const config::Config& config,
                   ShutdownMode shutdown_mode = ShutdownMode::kDisable);
  ~ActuatorCommands();
  ActuatorCommands(const ActuatorCommands&) = delete;
  ActuatorCommands& operator=(const ActuatorCommands&) = delete;

  using StopRequests =
      std::vector<std::pair<std::shared_ptr<robot::action::ControlGate>, uint64_t>>;
  StopRequests RequestStopActions();
  static absl::Status CompleteStopActions(const StopRequests& requests);
  absl::Status StopBoardActions(const std::string& board_name, bool output_already_disabled);

 private:
  struct ControlledActuator {
    std::shared_ptr<robot::action::ControlGate> gate;
    std::string board_name;
  };
  void ShutdownActions(ShutdownMode mode);
  void AddService(const std::string& name, std::function<absl::Status()> action);

  rclcpp::Node& node_;
  const ShutdownMode shutdown_mode_;
  std::vector<ControlledActuator> gates_;
  std::vector<rclcpp::SubscriptionBase::SharedPtr> subscriptions_;
  std::vector<rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr> services_;
  rclcpp::TimerBase::SharedPtr watchdog_;
};
}  // namespace ros2_utils
