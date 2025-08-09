#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "config/proto/config.pb.h"
#include "robot/action/factory/action_factory.h"
#include "robot/action/proto/action_packet.pb.h"
#include <thread>
#include <list>
#include <chrono>
#include "ros2/node_runner.h"

class ActionSubscriber : public rclcpp::Node {
private:
  struct Actuator {
    std::string topic;
    std::unique_ptr<robot::action::ActionInterface> interface;
    std::pair<float, float> limits;
    robot::perception::EncoderDataMode encoder_data_mode;
    rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr subscription;
    robot::action::ActionPacket reusable_packet;  // Pre-allocated packet for reuse
  };

public:
  ActionSubscriber(const std::string& node_name, const int node_id, const config::Config& config) 
  : Node(node_name) {      
    robot::action::ActionFactory action_factory;

    for (const auto& single_action : config.robot().actions().single_actions()) {
      if (single_action.action_type() == robot::action::ActionType::ACTUATOR && single_action.node_id() == node_id) {
        const auto& action_proto = single_action.actuator();
        
        auto interface = action_factory.CreateAction(single_action);
        if (!interface) {
            RCLCPP_ERROR(this->get_logger(), "Failed to create action interface for actuator '%s'. Check hardware connection or permissions.", 
                         action_proto.actuator_name().c_str());
            continue;
        }

        // Add a new actuator to the list and get a stable reference to it.
        Actuator& actuator = actuators_.emplace_back(Actuator{
          .topic = single_action.subscribe_topic(),
          .interface = std::move(interface),
          .limits = {action_proto.operational_lower_limit(), action_proto.operational_upper_limit()},
          .encoder_data_mode = action_proto.encoder_data_mode()
        });

        actuator.reusable_packet.Clear();
        actuator.reusable_packet.set_preset(robot::action::PresetCommand::PRESET_ENABLE_TORQUE);
        actuator.interface->SetAction(actuator.reusable_packet);
        
        // With std::list, the reference captured here is stable and will not be
        // invalidated by adding more elements to the list. The callback only
        // needs to set the position.
        auto callback = [this, &actuator](const std_msgs::msg::Float32::SharedPtr msg) {
            float action_value = msg->data;
            float mapped_position;
            switch(actuator.encoder_data_mode) {
              case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_RAW:
                mapped_position = action_value;
                break;
              case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_NORMALIZED_ZERO_TO_ONE:
                mapped_position = actuator.limits.first + action_value * (actuator.limits.second - actuator.limits.first);
                break;
              case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_NORMALIZED_MINUS_ONE_TO_ONE:
                mapped_position = actuator.limits.first + 
                                  ((action_value + 1.0f) / 2.0f) * (actuator.limits.second - actuator.limits.first);
                break;
              case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_NORMALIZED_RADIAN:
                mapped_position = actuator.limits.first + ((action_value + (static_cast<float>(M_PI) / 2.0f)) / static_cast<float>(M_PI)) *
                                  (actuator.limits.second - actuator.limits.first);
                break;
              default:
                RCLCPP_WARN(this->get_logger(), "Invalid encoder data mode for actuator '%s'!", actuator.topic.c_str());
                return;
            }
            
            // Reuse pre-allocated packet for optimal performance
            actuator.reusable_packet.Clear();
            actuator.reusable_packet.set_position(mapped_position);
            actuator.interface->SetAction(actuator.reusable_packet);
        };
        
        actuator.subscription = this->create_subscription<std_msgs::msg::Float32>(
            actuator.topic, 10, callback);
      }
    }

    if (actuators_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No actuators found in configuration for node_id %d!", node_id);
      return;
    }

    RCLCPP_INFO(this->get_logger(), "Actuator subscriber node started with %zu actuators for node_id %d!", actuators_.size(), node_id);
  }

  ~ActionSubscriber() {
    std::vector<std::thread> threads;
    
    // Start all shutdown threads in parallel for graceful shutdown.
    for (auto& actuator : actuators_) {
      threads.emplace_back([&actuator]() {
        actuator.reusable_packet.Clear();
        actuator.reusable_packet.set_preset(robot::action::PresetCommand::PRESET_GRACEFUL_SHUTDOWN);
        actuator.interface->SetAction(actuator.reusable_packet);
      });
    }
    
    for (auto& thread : threads) {
      thread.join();
    }
  }

private:
  std::list<Actuator> actuators_;
};

int main(int argc, char * argv[]) {
  return ros2_utils::RunNode<ActionSubscriber>(argc, argv, "actuator_subscriber");
} 