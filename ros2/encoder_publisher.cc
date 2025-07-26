#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include <memory>

// TODO: Separate each encoder into a separate node.
class EncoderPublisher : public rclcpp::Node {
public:
  EncoderPublisher() : Node("encoder_publisher") {
    config::Config config = config::config_util::LoadConfig("config/config_preset/so100_with_follower.pbtxt");
    
    robot::perception::PerceptionFactory perception_factory;
    
    for (const auto& single_perception : config.robot().perceptions().single_perception()) {
      const auto& sensor_proto = single_perception.sensor();
      if (sensor_proto.sensor_type() == robot::perception::SensorType::ENCODER) {
        encoders_.push_back(perception_factory.CreatePerception(sensor_proto));
        encoder_limits_.push_back(std::make_pair(sensor_proto.encoder_config().operational_lower_limit(), sensor_proto.encoder_config().operational_upper_limit()));
        RCLCPP_INFO(this->get_logger(), "Found encoder in configuration");
      }
    }
    
    if (encoders_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No encoders found in configuration!");
      return;
    }
    
    publisher_ = this->create_publisher<std_msgs::msg::Float32MultiArray>("encoder_positions", 10);
    
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(33), // 30 Hz update rate
      std::bind(&EncoderPublisher::publish_encoder_data, this));
      
    RCLCPP_INFO(this->get_logger(), "Encoder publisher node started with %zu encoders!", encoders_.size());
  }

private:
  void publish_encoder_data() {
    if (encoders_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No encoders initialized!");
      return;
    }
    
    try {
      auto message = std_msgs::msg::Float32MultiArray();
      message.data.reserve(encoders_.size());
      
      for (size_t i = 0; i < encoders_.size(); ++i) {
        auto data_packet = encoders_[i]->GetData();
        if (!data_packet) {
          RCLCPP_ERROR(this->get_logger(), "Failed to get encoder %zu data!", i);
          message.data.push_back(0.0f); // Use 0.0 as fallback
          continue;
        }
        
        // TODO: Reconsider the proto message to primitive types.
        float position = data_packet->encoder_perception().position();
        RCLCPP_INFO(this->get_logger(), "Encoder %zu position: %f", i, position);
        auto normalized_position = 2.0f * (position - encoder_limits_[i].first) / (encoder_limits_[i].second - encoder_limits_[i].first) - 1.0f;
        message.data.push_back(normalized_position);
      }
      
      // Publish the message with all encoder positions
      publisher_->publish(message);
      
      RCLCPP_INFO(this->get_logger(), "Publishing %zu encoder positions", message.data.size());
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error reading encoders: %s", e.what());
    }
  }
  
  rclcpp::Publisher<std_msgs::msg::Float32MultiArray>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::vector<std::unique_ptr<robot::perception::PerceptionInterface>> encoders_;
  std::vector<std::pair<float, float>> encoder_limits_;
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<EncoderPublisher>());
  rclcpp::shutdown();
  return 0;
} 