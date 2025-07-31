#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include "robot/action/factory/action_factory.h"
#include "robot/action/proto/action_packet.pb.h"
#include <thread>
#include <list>
#include <chrono>

class ActionSubscriber : public rclcpp::Node {
private:
  struct Actuator {
    std::string topic;
    std::unique_ptr<robot::action::ActionInterface> interface;
    std::pair<float, float> limits;
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
          .limits = {action_proto.operational_lower_limit(), action_proto.operational_upper_limit()}
        });

        actuator.reusable_packet.Clear();
        actuator.reusable_packet.set_preset(robot::action::PresetCommand::PRESET_ENABLE_TORQUE);
        actuator.interface->SetAction(actuator.reusable_packet);
        
        // With std::list, the reference captured here is stable and will not be
        // invalidated by adding more elements to the list. The callback only
        // needs to set the position.
        auto callback = [this, &actuator](const std_msgs::msg::Float32::SharedPtr msg) {
            float action_value = msg->data; // action_value is in [-1, 1] range
            float mapped_position = actuator.limits.first + 
                                    ((action_value + 1.0f) / 2.0f) * (actuator.limits.second - actuator.limits.first);
            
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
  rclcpp::init(argc, argv);
  
  if (argc < 4) {
    RCLCPP_ERROR(rclcpp::get_logger("actuator_subscriber"), 
                 "Usage: actuator_subscriber <node_id> <node_name> <config_path>");
    return 1;
  }
  
  int node_id = std::stoi(argv[1]);
  std::string node_name = argv[2];
  std::string config_path = argv[3];
  
  config::Config config = config::config_util::LoadConfig(config_path);
  
  rclcpp::spin(std::make_shared<ActionSubscriber>(node_name, node_id, config));
  rclcpp::shutdown();
  return 0;
} 