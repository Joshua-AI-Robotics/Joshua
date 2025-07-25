#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include "robot/actuation/factory/actuation_factory.h"
#include <thread>

class ActuationSubscriber : public rclcpp::Node {

public:
  ActuationSubscriber(const std::string& actuation_topic) : Node("actuation_subscriber") {
    subscription_ = this->create_subscription<std_msgs::msg::Float32MultiArray>(
      "actuation_input", 10, std::bind(&ActuationSubscriber::actuation_callback, this, std::placeholders::_1));
    
    config::Config config = config::config_util::LoadConfig("config/config_preset/so100_with_example_ai.pbtxt");
    
    robot::actuation::ActuationFactory actuation_factory;

    for (const auto& single_actuation : config.robot().actuations().single_actuation()) {
      const auto& actuator_proto = single_actuation.actuator();
      actuators_.push_back(actuation_factory.CreateActuator(actuator_proto));
    }

    for (const auto& single_actuation : config.robot().actuations().single_actuation()) {
      const auto& actuator_proto = single_actuation.actuator();
      // TODO: Move operational limits to actuator proto, not motor specific proto.
      actuation_limits_.push_back(std::make_pair(actuator_proto.sts3215_config().operational_lower_limit(), actuator_proto.sts3215_config().operational_upper_limit()));
    }

    if (actuators_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No actuators found in configuration!");
      return;
    }

    for (const auto& actuator : actuators_) {
      actuator->SetTorque(1);
      actuator->SetMiddlePosition();
    }

    RCLCPP_INFO(this->get_logger(), "Actuation subscriber node started with %zu actuators!", actuators_.size());
  }

  ~ActuationSubscriber() {
    std::vector<std::thread> threads;
    
    // Start all shutdown threads in parallel
    for (const auto& actuator : actuators_) {
      threads.emplace_back([&actuator]() {
        actuator->GracefulShutdown();
      });
    }
    
    // Wait for all threads to complete
    for (auto& thread : threads) {
      thread.join();
    }
  }

private:
  void actuation_callback(const std_msgs::msg::Float32MultiArray::SharedPtr actuation_msg) {
    for (size_t i = 0; i < actuation_msg->data.size() && i < actuators_.size(); ++i) {
      float actuation_value = actuation_msg->data[i];
      RCLCPP_INFO(this->get_logger(), "Actuator %zu: %f", i, actuation_value);
      
      auto middle_value = (actuation_limits_[i].first + actuation_limits_[i].second) / 2;
      actuators_[i]->SetPosition(middle_value + actuation_value * kActuationStep);
    }
  }

  rclcpp::Subscription<std_msgs::msg::Float32MultiArray>::SharedPtr subscription_;
  std::vector<std::unique_ptr<robot::actuation::ActuationInterface>> actuators_;
  std::vector<std::pair<float, float>> actuation_limits_;
  
  const int kActuationStep = 30; // TODO: Move this into local namespace.
};

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ActuationSubscriber>("keyboard_input"));
  rclcpp::shutdown();
  return 0;
} 