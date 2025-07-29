#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include <memory>

// TODO: Separate each encoder into a separate node.
class EncoderPublisher : public rclcpp::Node {
public:
  EncoderPublisher(const std::string& node_name, const std::string& topic_name, const config::Config& config, int node_id) 
    : Node(node_name) {
    robot::perception::PerceptionFactory perception_factory;
    
    for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
      if (single_perception.perception_type() == robot::perception::PerceptionType::ENCODER && 
          single_perception.node_id() == node_id) {
        const auto& encoder_proto = single_perception.encoder();
        encoders_.push_back(perception_factory.CreatePerception(single_perception));
        // TODO: Make this generic for all encoders.
        encoder_limits_.push_back(std::make_pair(
          encoder_proto.operational_lower_limit(), 
          encoder_proto.operational_upper_limit()));
        RCLCPP_INFO(this->get_logger(), "Found encoder '%s' in configuration for node_id %d", 
                   encoder_proto.encoder_name().c_str(), node_id);
      }
    }
    
    if (encoders_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No encoders found in configuration for node_id %d!", node_id);
      return;
    }
    
    publisher_ = this->create_publisher<std_msgs::msg::Float32MultiArray>(topic_name, 10);
    
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(16), // 60 Hz update rate
      std::bind(&EncoderPublisher::publish_encoder_data, this));
      
    RCLCPP_INFO(this->get_logger(), "Encoder publisher node started with %zu encoders for node_id %d!", 
               encoders_.size(), node_id);
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
        auto data = encoders_[i]->GetData();
        auto data_opt = std::any_cast<std::optional<uint16_t>>(data);
        if (!data_opt.has_value()) {
            RCLCPP_WARN(this->get_logger(), "Failed to get data from encoder %zu!", i);
            continue;
        }
        
        // Get encoder position and normalize to [-1, 1]
        uint16_t raw_position = *data_opt;
        float normalized_position = 2.0f * (static_cast<float>(raw_position) - encoder_limits_[i].first) / 
                                   (encoder_limits_[i].second - encoder_limits_[i].first) - 1.0f;
        
        // Clamp to [-1, 1] range
        normalized_position = std::max(-1.0f, std::min(1.0f, normalized_position));
        message.data.push_back(normalized_position);
      }
      
      publisher_->publish(message);
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error publishing encoder data: %s", e.what());
    }
  }
  
  rclcpp::Publisher<std_msgs::msg::Float32MultiArray>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  std::vector<std::unique_ptr<robot::perception::PerceptionInterface>> encoders_;
  std::vector<std::pair<float, float>> encoder_limits_;
};

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  
  if (argc < 4) {
    RCLCPP_ERROR(rclcpp::get_logger("encoder_publisher"), 
                 "Usage: encoder_publisher <config_path> <node_id> <node_name>");
    return 1;
  }
  
  std::string config_path = argv[1];
  int node_id = std::stoi(argv[2]);
  std::string node_name = argv[3];
  
  config::Config config = config::config_util::LoadConfig(config_path);
  
  // TODO: Update the subscription topic based on the operation mode and config.
  rclcpp::spin(std::make_shared<EncoderPublisher>(node_name, "encoder_positions", config, node_id));
  rclcpp::shutdown();
  return 0;
} 