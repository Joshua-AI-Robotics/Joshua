#include <string>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "ros2/node_runner.h"
#include "ros2/position_publishers.h"

class EncoderPublisher : public rclcpp::Node {
 public:
  EncoderPublisher(const std::string& node_name, const int node_id, const config::Config& config)
      : Node(node_name), positions_(*this, node_id, config.robot()) {}

 private:
  ros2_utils::PositionPublishers positions_;
};

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<EncoderPublisher>(argc, argv, "encoder_publisher");
}
