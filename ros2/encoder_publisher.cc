#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "config/proto/config.pb.h"
#include <memory>
#include <string>
#include <vector>
#include <utility>
#include <cmath>
#include "ros2/node_runner.h"

class EncoderPublisher : public rclcpp::Node {
private:
  struct Encoder {
    std::string topic;
    std::unique_ptr<robot::perception::PerceptionInterface> interface;
    std::pair<float, float> limits;
    rclcpp::Publisher<std_msgs::msg::Float32>::SharedPtr publisher;
    robot::perception::EncoderDataMode encoder_data_mode;
    rclcpp::TimerBase::SharedPtr timer;
  };

public:
  EncoderPublisher(const std::string& node_name, const int node_id, const config::Config& config) 
    : Node(node_name) {
    robot::perception::PerceptionFactory perception_factory;
    
    for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
      if (single_perception.perception_type() == robot::perception::PerceptionType::ENCODER && 
          static_cast<int>(single_perception.node_id()) == node_id) {
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
          .encoder_data_mode = encoder_proto.encoder_data_mode(),
          .timer = this->create_wall_timer(
            std::chrono::milliseconds(1000 / single_perception.publish_rate_hz()),
            std::bind(&EncoderPublisher::publish_encoder_data, this))
        });

        RCLCPP_INFO(this->get_logger(), "Found encoder '%s' in configuration for node_id %d. Publishing on topic: %s with data mode: %d", 
                   encoder_proto.encoder_name().c_str(), node_id, single_perception.publish_topic().c_str(), encoder_proto.encoder_data_mode());
      }
    }
    
    if (encoders_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No encoders found in configuration for node_id %d!", node_id);
      return;
    }
      
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

        if (!packet.ok()) {
            RCLCPP_WARN(this->get_logger(), "Failed to get data from encoder '%s'!", encoder.topic.c_str());
            continue;
        }
        
        // Check if packet contains position data
        if (!packet.value().has_position()) {
            RCLCPP_WARN(this->get_logger(), "Failed to get position data from encoder '%s'!", encoder.topic.c_str());
            continue;
        }
        
        float position_data = packet.value().position().position();

        switch(encoder.encoder_data_mode) {
          case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_RAW:
            break;
          case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_NORMALIZED_ZERO_TO_ONE:
            position_data = (position_data - encoder.limits.first) / 
                                (encoder.limits.second - encoder.limits.first);
            position_data = std::max(0.0f, std::min(1.0f, position_data));
            break;
          case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_NORMALIZED_MINUS_ONE_TO_ONE:
            position_data = 2.0f * (position_data - encoder.limits.first) / 
                                (encoder.limits.second - encoder.limits.first) - 1.0f;
            position_data = std::max(-1.0f, std::min(1.0f, position_data));
            break;
          case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_NORMALIZED_RADIAN:
            position_data = static_cast<float>(M_PI) * (position_data - encoder.limits.first) /
                                (encoder.limits.second - encoder.limits.first) - (static_cast<float>(M_PI) / 2.0f);
            position_data = std::max(-static_cast<float>(M_PI) / 2.0f, std::min(static_cast<float>(M_PI) / 2.0f, position_data));
            break;
          default:
            RCLCPP_WARN(this->get_logger(), "Invalid publish data mode for encoder '%s'!", encoder.topic.c_str());
            continue;
        }
        
        auto message = std_msgs::msg::Float32();
        message.data = position_data;
        encoder.publisher->publish(message);
      }
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error publishing encoder data: %s", e.what());
    }
  }
  
  
  std::vector<Encoder> encoders_;
};

int main(int argc, char* argv[]) {
  // For test run:
  // bazel run ros2:encoder_publisher -- test_encoder 1 config/config_preset/so100_leader_arm_encoder_publish.pbtxt
  return ros2_utils::RunNode<EncoderPublisher>(argc, argv, "encoder_publisher");
} 