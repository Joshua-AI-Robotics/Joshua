#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include "robot/action/factory/action_factory.h"
#include <thread>

class ActionSubscriber : public rclcpp::Node {
private:
  struct Actuator {
    std::string topic;
    std::unique_ptr<robot::action::ActionInterface> interface;
    std::pair<float, float> limits;
    rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr subscription;
  };

public:
  ActionSubscriber(const std::string& node_name, const int node_id, const config::Config& config) 
  : Node(node_name) {      
    robot::action::ActionFactory action_factory;

    for (const auto& single_action : config.robot().actions().single_actions()) {
      if (single_action.action_type() == robot::action::ActionType::ACTUATOR && single_action.node_id() == node_id) {
        const auto& action_proto = single_action.actuator();
        
        // First, create the interface and check if it's valid.
        auto interface = action_factory.CreateAction(single_action);
        if (!interface) {
            RCLCPP_ERROR(this->get_logger(), "Failed to create action interface for actuator '%s'. Check hardware connection or permissions.", 
                         action_proto.actuator_name().c_str());
            continue; // Skip this actuator if initialization failed.
        }

        interface->SetTorque(true);

        // Add the fully initialized actuator to our vector.
        actuators_.emplace_back(Actuator{
          .topic = single_action.subscribe_topic(),
          .interface = std::move(interface),
          .limits = {action_proto.operational_lower_limit(), action_proto.operational_upper_limit()}
        });

        // The callback now captures the actuator's index, which is safe from vector reallocations.
        const size_t actuator_index = actuators_.size() - 1;
        auto callback = [this, actuator_index](const std_msgs::msg::Float32::SharedPtr msg) {
            Actuator& actuator = this->actuators_[actuator_index];
            float action_value = msg->data; // action_value is in [-1, 1] range
            
            // Map the normalized action value from [-1, 1] to the actuator's operational range [min, max].
            float mapped_position = actuator.limits.first + 
                                    ((action_value + 1.0f) / 2.0f) * (actuator.limits.second - actuator.limits.first);
            
            actuator.interface->SetPosition(mapped_position);
        };

        // Assign the subscription to the newly added actuator.
        actuators_.back().subscription = this->create_subscription<std_msgs::msg::Float32>(
            actuators_.back().topic, 10, callback);
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
    for (const auto& actuator : actuators_) {
      threads.emplace_back([&actuator]() {
        actuator.interface->GracefulShutdown();
      });
    }
    
    for (auto& thread : threads) {
      thread.join();
    }
  }

private:
  std::vector<Actuator> actuators_;
  const int kActionStep = 30; // TODO: Move this into local namespace.
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