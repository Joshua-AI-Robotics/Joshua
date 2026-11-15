#include "ros2/action_subscriber.h"
#include "ros2/node_runner.h"

class ActionSubscriber : public rclcpp::Node {
 public:
  ActionSubscriber(const std::string& name, int id, const config::Config& config)
      : Node(name), commands_(*this) {
    ros2_utils::SetLogNodeName(get_logger().get_name());
    bool complete = false;
    joshua::ScopeExit rollback([&] {
      if (!complete) commands_.ShutdownActions();
    });
    commands_.Initialize(id, config);
    complete = true;
  }
  ~ActionSubscriber() override {
    commands_.ShutdownActions();
  }

 private:
  ActuatorCommands commands_;
};

#ifndef JOSHUA_NODE_TEST
JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<ActionSubscriber>(argc, argv, "actuator_subscriber", 2);
}
#endif
