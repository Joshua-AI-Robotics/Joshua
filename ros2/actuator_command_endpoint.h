#pragma once

#include <map>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "ros2/actuator_session.h"
#include "std_msgs/msg/string.hpp"

namespace ros2_actuator {
// Configured command/status topics. Acknowledgments describe driver outcomes,
// not DDS publication. All producers share one policy and one hardware owner.
class CommandEndpoint {
 public:
  CommandEndpoint(rclcpp::Node& node,
                  Device device,
                  std::shared_ptr<robot::action::ActionInterface> action);

 private:
  google::protobuf::Struct Process(const google::protobuf::Struct& request);
  ActuatorSession session_;
  std::string session_id_;
  std::map<std::string, google::protobuf::Struct> responses_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  rclcpp::TimerBase::SharedPtr monitor_;
};
}  // namespace ros2_actuator
