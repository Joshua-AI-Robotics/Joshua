#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"

class ActuationSubscriber : public rclcpp::Node {
public:
  ActuationSubscriber() : Node("actuation_subscriber") {
    subscription_ = this->create_subscription<std_msgs::msg::Float32MultiArray>(
      "actuation", 10, std::bind(&ActuationSubscriber::callback, this, _1));
  }

private:
  void callback(const std_msgs::msg::Float32MultiArray actuation_msg) {
    RCLCPP_INFO(this->get_logger(), "Received actuation message: %f", actuation_msg.data[0]);
  }

  rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr subscription_;
};