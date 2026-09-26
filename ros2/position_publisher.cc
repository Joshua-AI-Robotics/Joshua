#include <string>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "ros2/position_publishers.h"

class PositionPublisher : public rclcpp::Node {
 public:
  PositionPublisher(const std::string& node_name, const int node_id, const config::Config& config)
      : Node(node_name), positions_(*this, node_id, config.robot()) {}

 private:
  ros2_utils::PositionPublishers positions_;
};

namespace ros2_utils {
std::shared_ptr<rclcpp::Node> CreateNode(const std::string& name,
                                         int id,
                                         const config::Config& config) {
  return std::make_shared<PositionPublisher>(name, id, config);
}
}  // namespace ros2_utils
