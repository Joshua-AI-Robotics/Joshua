#include <memory>
#include <string>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "ros2/logging.h"
#include "ros2/node_runner.h"
#include "ros2/utils/actuator_commands.h"

class ActionSubscriber : public rclcpp::Node {
 public:
  ActionSubscriber(const std::string& node_name, int node_id, const config::Config& config)
      : Node(node_name) {
    ros2_utils::SetLogNodeName(get_logger().get_name());
    commands_ = std::make_unique<ros2_utils::ActuatorCommands>(*this, node_id, config);
  }

 private:
  std::unique_ptr<ros2_utils::ActuatorCommands> commands_;
};

#ifndef JOSHUA_NODE_TEST
int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<ActionSubscriber>(argc, argv, "actuator_subscriber");
}
#endif
