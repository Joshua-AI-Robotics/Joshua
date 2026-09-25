#pragma once

#include <memory>
#include <string>

#include "google/protobuf/struct.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "ros2/actuator_session.h"
#include "std_msgs/msg/string.hpp"

namespace mhs {
// No device access: transports the existing tool requests over configured ROS topics.
class RosClient {
 public:
  explicit RosClient(const ros2_actuator::Device& device);
  google::protobuf::Struct Request(google::protobuf::Struct request);
  google::protobuf::Struct Stop();

 private:
  google::protobuf::Struct Exchange(google::protobuf::Struct request);
  std::shared_ptr<rclcpp::Node> node_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  rclcpp::Publisher<std_msgs::msg::String>::SharedPtr publisher_;
  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  std::string device_id_, prefix_, pending_id_, session_id_;
  uint64_t sequence_ = 0;
  bool received_ = false, uncertain_ = false;
  google::protobuf::Struct response_;
};
}  // namespace mhs
