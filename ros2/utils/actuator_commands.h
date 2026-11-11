#pragma once

#include <list>
#include <memory>
#include <string>
#include <utility>

#include "config/proto/config.pb.h"
#include "robot/action/interfaces/action_interface.h"
#include "robot/action/proto/action_packet.pb.h"

namespace rclcpp {
class Node;
class SubscriptionBase;
}  // namespace rclcpp

namespace ros2_utils {

// Owns command subscriptions on a caller-owned node; it is not a ROS node.
// Standalone owners enable/teardown actuators. Mixed bus owners pass false and
// manage the board lifecycle themselves, keeping startup disabled.
class ActuatorCommands {
 public:
  ActuatorCommands(rclcpp::Node& node,
                   int node_id,
                   const config::Config& config,
                   bool auto_enable = true);
  ~ActuatorCommands();
  ActuatorCommands(const ActuatorCommands&) = delete;
  ActuatorCommands& operator=(const ActuatorCommands&) = delete;

 private:
  struct Actuator {
    std::string topic;
    std::shared_ptr<robot::action::ActionInterface> interface;
    std::pair<float, float> limits;
    std::shared_ptr<rclcpp::SubscriptionBase> subscription;
    robot::action::ActionPacket reusable_packet;
  };
  bool manage_lifecycle_;
  std::list<Actuator> actuators_;
};

}  // namespace ros2_utils
