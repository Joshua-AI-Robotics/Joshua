#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"
#include <random>
#include <chrono>

class MockActuationPublisher : public rclcpp::Node {
public:
  MockActuationPublisher() : Node("mock_actuation_publisher") {
    publisher_ = this->create_publisher<std_msgs::msg::Float32MultiArray>(
      "mock_actuation_input", 10);
    
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(100),
      std::bind(&MockActuationPublisher::publish_mock_actuation_data, this));
    
    std::random_device rd;
    random_generator_ = std::mt19937(rd());
    distribution_ = std::uniform_real_distribution<float>(-1.0f, 1.0f);
    
    RCLCPP_INFO(this->get_logger(), "Mock actuation publisher started!");
    RCLCPP_INFO(this->get_logger(), "Publishing 6-element arrays on topic: /mock_actuation_input");
  }

private:
  void publish_mock_actuation_data() {
    auto message = std_msgs::msg::Float32MultiArray();
    
    // Generate 6 random float values between -1 and 1
    message.data.reserve(6);
    for (int i = 0; i < 6; ++i) {
      float random_value = distribution_(random_generator_);
      message.data.push_back(random_value);
    }
    
    // Publish the message
    publisher_->publish(message);
    
    // Log the published values
    RCLCPP_INFO(this->get_logger(), "Published actuation array: [%.3f, %.3f, %.3f, %.3f, %.3f, %.3f]",
                message.data[0], message.data[1], message.data[2],
                message.data[3], message.data[4], message.data[5]);
  }
  
  rclcpp::Publisher<std_msgs::msg::Float32MultiArray>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::mt19937 random_generator_;
  std::uniform_real_distribution<float> distribution_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<MockActuationPublisher>());
  rclcpp::shutdown();
  return 0;
}
