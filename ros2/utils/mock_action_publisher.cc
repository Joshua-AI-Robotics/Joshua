#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"
#include <random>
#include <chrono>

class MockActionPublisher : public rclcpp::Node {
public:
  MockActionPublisher() : Node("mock_action_publisher") {
    publisher_ = this->create_publisher<std_msgs::msg::Float32MultiArray>(
      "mock_action_input", 10);
    
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      std::bind(&MockActionPublisher::publish_mock_action_data, this));
    
    std::random_device rd;
    random_generator_ = std::mt19937(rd());
    distribution_ = std::uniform_real_distribution<float>(-1.0f, 1.0f);
    
    RCLCPP_INFO(this->get_logger(), "Mock action publisher started!");
    RCLCPP_INFO(this->get_logger(), "Publishing 6-element arrays on topic: /mock_action_input");
  }

private:
  void publish_mock_action_data() {
    auto message = std_msgs::msg::Float32MultiArray();
    
    // Generate 6 random float values between -1 and 1
    message.data.reserve(6);
    for (int i = 0; i < 6; ++i) {
      float random_value = distribution_(random_generator_);
      message.data.push_back(random_value);
    }
    
    // Publish the message
    publisher_->publish(message);
  }
  
  rclcpp::Publisher<std_msgs::msg::Float32MultiArray>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::mt19937 random_generator_;
  std::uniform_real_distribution<float> distribution_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MockActionPublisher>());
  rclcpp::shutdown();
  return 0;
}
