#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include "robot/action/factory/action_factory.h"
#include <thread>

class ActionSubscriber : public rclcpp::Node {

public:
  ActionSubscriber(const std::string& node_name, const std::string& topic_name, const config::Config& config) 
  : Node(node_name) {
    // TODO: Currently actions operates based on encoder positions with fixed and ordered number of encoders.
    // This needs to be updated by adding a config that each perception has a unique name and id and need to map to actions.
    if (config.operation_mode() == config::OperationMode::MODE_TELEOPERATE) {
      subscription_topic_name_ = "encoder_positions";
    }
    else if (config.operation_mode() == config::OperationMode::MODE_INFERENCE) {
      subscription_topic_name_ = "mock_action_input";
    }
    else {
      RCLCPP_ERROR(this->get_logger(), "Invalid operation mode!");
      return;
    }

    subscription_ = this->create_subscription<std_msgs::msg::Float32MultiArray>(
      subscription_topic_name_, 10, std::bind(&ActionSubscriber::action_callback, this, std::placeholders::_1));
    
    robot::action::ActionFactory action_factory;

    for (const auto& single_action : config.robot().actions().single_actions()) {
      actions_.push_back(action_factory.CreateAction(single_action));
    }

    for (const auto& single_action : config.robot().actions().single_actions()) {
      if (single_action.action_type() == robot::action::ActionType::ACTUATOR) {
        const auto& action_proto = single_action.actuator();
        action_limits_.push_back(std::make_pair(action_proto.operational_lower_limit(), action_proto.operational_upper_limit()));
      }
      // TODO: Add handling for other action types when they are implemented
    }

    if (actions_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No actions found in configuration!");
      return;
    }

    for (const auto& action : actions_) {
      action->SetTorque(true);
    }

    RCLCPP_INFO(this->get_logger(), "Action subscriber node started with %zu actions!", actions_.size());
  }

  ~ActionSubscriber() {
    std::vector<std::thread> threads;
    
    // Start all shutdown threads in parallel
    for (const auto& action : actions_) {
      threads.emplace_back([&action]() {
        action->GracefulShutdown();
      });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
      thread.join();
    }
  }

private:
  void action_callback(const std_msgs::msg::Float32MultiArray::SharedPtr action_msg) {
    for (size_t i = 0; i < action_msg->data.size() && i < actions_.size(); ++i) {
      float action_value = action_msg->data[i]; // normalized in [-1, 1]

      float mapped_position = action_limits_[i].first + (action_value + 1.0f) * (action_limits_[i].second - action_limits_[i].first) / 2.0f;

      actions_[i]->SetPosition(mapped_position);
    }
  }
  
  std::string subscription_topic_name_;
  rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr subscription_;
  std::vector<std::unique_ptr<robot::action::ActionInterface>> actions_;
  std::vector<std::pair<float, float>> action_limits_;
  
  const int kActionStep = 30; // TODO: Move this into local namespace.
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  
  if (argc < 3) {
    RCLCPP_ERROR(rclcpp::get_logger("actuator_subscriber"), 
                 "Usage: actuator_subscriber <config_path> <node_name>");
    return 1;
  }
  
  std::string config_path = argv[1];
  std::string node_name = argv[2];
  
  config::Config config = config::config_util::LoadConfig(config_path);
  
  // TODO: Update the subscription topic based on the operation mode and config.
  rclcpp::spin(std::make_shared<ActionSubscriber>(node_name, "actuator_commands", config));
  rclcpp::shutdown();
  return 0;
} 