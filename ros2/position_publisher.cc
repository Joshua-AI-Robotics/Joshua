#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/node_runner.h"
#include "ros2/utils/packet_parser.h"

class PositionPublisher : public rclcpp::Node {
 public:
  PositionPublisher(const std::string& node_name, int node_id, const config::Config& config)
      : Node(node_name) {
    for (const auto& sensor : config.robot().perceptions().single_perceptions()) {
      if (sensor.node().id() != static_cast<uint32_t>(node_id) ||
          sensor.sensor_type() != robot::perception::POSITION)
        continue;
      auto result =
          robot::perception::PerceptionFactory::CreatePerception(sensor, config.robot().boards());
      if (!result.ok()) throw std::runtime_error(result.status().ToString());
      auto interface = std::shared_ptr<robot::perception::PerceptionInterface>(std::move(*result));
      for (const auto& pub : sensor.node().publishers()) {
        auto publisher = ros2_utils::CreatePositionMessagePublisher(*this, pub, sensor);
        if (!publisher.ok()) throw std::invalid_argument(publisher.status().ToString());
        timers_.push_back(create_wall_timer(
            std::chrono::duration<double>(1.0 / pub.publish_rate_hz()),
            [interface, publish = *publisher, logger = get_logger()]() {
              try {
                auto packet = interface->GetData();
                if (!packet.ok()) {
                  RCLCPP_WARN(
                      logger, "Cannot read position: %s", packet.status().ToString().c_str());
                  return;
                }
                auto position = ros2_utils::RequirePerceptionPosition(*packet);
                if (!position.ok()) {
                  RCLCPP_ERROR(
                      logger, "Invalid position: %s", position.status().ToString().c_str());
                  return;
                }
                const auto status = publish(*position);
                if (!status.ok()) {
                  RCLCPP_ERROR(logger, "Cannot publish position: %s", status.ToString().c_str());
                }
              } catch (const std::exception& error) {
                RCLCPP_ERROR(logger, "Error publishing position: %s", error.what());
              }
            }));
      }
    }
  }

 private:
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;
};
#ifndef JOSHUA_NODE_TEST
int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<PositionPublisher>(argc, argv, "position_publisher");
}
#else
std::shared_ptr<rclcpp::Node> MakePositionPublisherForTest(const config::Config& config) {
  return ros2_utils::CreateValidatedNode(
      "position_feedback_test",
      1,
      config,
      [](const std::string& name, int id, const config::Config& validated_config) {
        return std::make_shared<PositionPublisher>(name, id, validated_config);
      });
}
#endif
