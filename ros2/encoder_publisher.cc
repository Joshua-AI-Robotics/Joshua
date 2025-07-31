#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "config/proto/robot.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "config/config_utils.h"
#include <memory>
#include <string>
#include <vector>
#include <utility>

class EncoderPublisher : public rclcpp::Node {
private:
  struct Encoder {
    std::string topic;
    std::unique_ptr<robot::perception::PerceptionInterface> interface;
    std::pair<float, float> limits;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr publisher;
    bool publish_unnormalized_data = false;
  };

public:
  EncoderPublisher(const std::string& node_name, const int node_id, const config::Config& config) 
    : Node(node_name) {
    robot::perception::PerceptionFactory perception_factory;
    
    for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
      if (single_perception.perception_type() == robot::perception::PerceptionType::ENCODER && 
          single_perception.node_id() == node_id) {
        const auto& encoder_proto = single_perception.encoder();
        
        // First, create the interface and check if it's valid.
        auto interface = perception_factory.CreatePerception(single_perception);
        if (!interface) {
            RCLCPP_ERROR(this->get_logger(), "Failed to create perception interface for encoder '%s'. Check hardware connection or permissions.", 
                         encoder_proto.encoder_name().c_str());
            continue; // Skip this encoder if initialization failed.
        }

        encoders_.emplace_back(Encoder{
          .topic = single_perception.publish_topic(),
          .interface = std::move(interface),
          .limits = {encoder_proto.operational_lower_limit(), encoder_proto.operational_upper_limit()},
          .publisher = this->create_publisher<std_msgs::msg::Float32>(single_perception.publish_topic(), 10),
          .publish_unnormalized_data = encoder_proto.publish_unnormalized_data()
        });

        RCLCPP_INFO(this->get_logger(), "Found encoder '%s' in configuration for node_id %d. Publishing on topic: %s", 
                   encoder_proto.encoder_name().c_str(), node_id, single_perception.publish_topic().c_str());
      }
    }
    
    if (encoders_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No encoders found in configuration for node_id %d!", node_id);
      return;
    }
    
    timer_ = this->create_wall_timer(
      std::chrono::milliseconds(16), // 60 Hz update rate
      std::bind(&EncoderPublisher::publish_encoder_data, this));
      
    RCLCPP_INFO(this->get_logger(), "Encoder publisher node started with %zu encoders for node_id %d!", 
               encoders_.size(), node_id);
  }

private:
  void publish_encoder_data() {
    if (encoders_.empty()) {
      RCLCPP_WARN(this->get_logger(), "No encoders initialized, skipping publish cycle.");
      return;
    }
    
    try {
      for (auto& encoder : encoders_) {
        auto packet = encoder.interface->GetData();
        
        // Check if packet contains position data
        if (!packet.has_position()) {
            RCLCPP_WARN(this->get_logger(), "Failed to get position data from encoder '%s'!", encoder.topic.c_str());
            continue;
        }
        
        // Get encoder position and normalize to [-1, 1]
        float raw_position = packet.position().position();
        float position_data;

        if (encoder.publish_unnormalized_data) {
          position_data = raw_position;
        } else {
          position_data = 2.0f * (raw_position - encoder.limits.first) / 
                                (encoder.limits.second - encoder.limits.first) - 1.0f;
          position_data = std::max(-1.0f, std::min(1.0f, position_data));
        }
        
        auto message = std_msgs::msg::Float32();
        message.data = position_data;
        encoder.publisher->publish(message);
      }
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error publishing encoder data: %s", e.what());
    }
  }
  
  rclcpp::TimerBase::SharedPtr timer_;
  std::vector<Encoder> encoders_;
};

int main(int argc, char* argv[]) {
  rclcpp::init(argc, argv);
  
  if (argc < 4) {
    RCLCPP_ERROR(rclcpp::get_logger("encoder_publisher"), 
                 "Usage: encoder_publisher <node_name> <node_id> <config_path>");
    return 1;
  }
  
  std::string node_name = argv[1];
  int node_id = std::stoi(argv[2]);
  std::string config_path = argv[3];
  
  config::Config config = config::config_util::LoadConfig(config_path);
  
  rclcpp::spin(std::make_shared<EncoderPublisher>(node_name, node_id, config));
  rclcpp::shutdown();
  return 0;
} 