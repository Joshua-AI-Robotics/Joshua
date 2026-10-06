#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/logging.h"
#include "ros2/node_runner.h"
#include "ros2/utils/packet_parser.h"

class PositionPublisher : public rclcpp::Node {
 public:
  PositionPublisher(const std::string& node_name, int node_id, const config::Config& config)
      : Node(node_name) {
    ros2_utils::SetLogNodeName(get_logger().get_name());
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
            [interface, publish = *publisher]() {
              try {
                auto packet = interface->GetData();
                if (!packet.ok()) {
                  JOSHUA_LOG(WARNING) << "Cannot read position: " << packet.status().ToString();
                  return;
                }
                auto position = ros2_utils::RequirePerceptionPosition(*packet);
                if (!position.ok()) {
                  JOSHUA_LOG(ERROR) << "Invalid position: " << position.status().ToString();
                  return;
                }
                const auto status = publish(*position);
                if (!status.ok()) {
                  JOSHUA_LOG(ERROR) << "Cannot publish position: " << status.ToString();
                }
              } catch (const std::exception& error) {
                JOSHUA_LOG(ERROR) << "Error publishing position: " << error.what();
              }
            }));
      }
    }
  }

 private:
  // TODO(hmoon): Integrate acquisition scheduling and message timestamps with
  // the planned system-wide clock (including PTP synchronization), retaining
  // config-driven rates and sharing each sensor reading across its publishers.
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;
};
int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<PositionPublisher>(argc, argv, "position_publisher");
}
