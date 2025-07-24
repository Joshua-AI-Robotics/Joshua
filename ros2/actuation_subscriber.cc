#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "config/proto/robot.pb.h"
#include "config/config_utils.h"
#include "robot/actuation/factory/actuation_factory.h"
#include <gflags/gflags.h>

class ActuationSubscriber : public rclcpp::Node {
public:
  ActuationSubscriber(const std::string& actuation_topic) : Node("actuation_subscriber") {

    // TODO: parse gflags to select actuation topic.
    
    // Default is keyboard input.
    subscription_ = this->create_subscription<std_msgs::msg::String>(
      "keyboard_input", 10, std::bind(&ActuationSubscriber::keyboard_callback, this, std::placeholders::_1));

    config::Config config = config::config_util::LoadConfig("config/config_preset/so100_with_example_ai.pbtxt");
    
    robot::actuation::ActuationFactory actuation_factory;

    for (const auto& single_actuation : config.robot().actuations().single_actuation()) {
      const auto& actuator_proto = single_actuation.actuator();
      actuators_.push_back(actuation_factory.CreateActuator(actuator_proto));
    }

    if (actuators_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No actuators found in configuration!");
      return;
    }

    RCLCPP_INFO(this->get_logger(), "Actuation subscriber node started with %zu actuators!", actuators_.size());
  }

private:
  void keyboard_callback(const std_msgs::msg::String::SharedPtr keyboard_msg) {
    RCLCPP_INFO(this->get_logger(), "Received keyboard input: %s", keyboard_msg->data.c_str());
    
    // TODO: Implement actuation logic based on keyboard input
    // For now, just log the received key
  }

  rclcpp::Subscription<std_msgs::msg::String>::SharedPtr subscription_;
  std::vector<std::unique_ptr<robot::actuation::ActuationInterface>> actuators_;
};

int main(int argc, char * argv[]) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_logtostderr = 1;

  gflags::ParseCommandLineFlags(&argc, &argv, true);

  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<ActuationSubscriber>("keyboard_input"));
  rclcpp::shutdown();
  return 0;
} 