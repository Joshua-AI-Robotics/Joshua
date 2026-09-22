#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/node_runner.h"
#include "ros2/utils/mapped_message.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"

class PositionPublisher : public rclcpp::Node {
 public:
  PositionPublisher(const std::string& node_name, int node_id, const config::Config& config)
      : Node(node_name) {
    const auto validation = config::ValidateConfig(config);
    if (!validation.ok()) throw std::invalid_argument(validation.ToString());
    // Resolve every message/path first: a bad later endpoint must not open an
    // earlier sensor's hardware as a side effect of partial initialization.
    std::vector<std::shared_ptr<ros2_utils::MappedMessage>> codecs;
    for (const auto& sensor : config.robot().perceptions().single_perceptions()) {
      if (sensor.node().id() != static_cast<uint32_t>(node_id) ||
          sensor.sensor_type() != robot::perception::POSITION)
        continue;
      for (const auto& pub : sensor.node().publishers()) {
        auto codec =
            ros2_utils::MappedMessage::Create(pub.ros2_data_type(), pub.scalar_mapping(), true);
        if (!codec.ok())
          throw std::invalid_argument(pub.topic() + ": " + codec.status().ToString());
        codecs.push_back(*codec);
      }
    }
    size_t endpoint = 0;
    for (const auto& sensor : config.robot().perceptions().single_perceptions()) {
      if (sensor.node().id() != static_cast<uint32_t>(node_id) ||
          sensor.sensor_type() != robot::perception::POSITION)
        continue;
      auto result =
          robot::perception::PerceptionFactory::CreatePerception(sensor, config.robot().boards());
      if (!result.ok()) throw std::runtime_error(result.status().ToString());
      auto interface = std::shared_ptr<robot::perception::PerceptionInterface>(std::move(*result));
      for (const auto& pub : sensor.node().publishers()) {
        auto codec = codecs.at(endpoint++);
        auto publisher =
            create_generic_publisher(pub.topic(),
                                     codec->type_name(),
                                     ros2_utils::CreateQosSetting(sensor.node().qos_setting()));
        timers_.push_back(create_wall_timer(
            std::chrono::duration<double>(1.0 / pub.publish_rate_hz()),
            [interface, codec, publisher, logger = get_logger()]() {
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
                auto message = codec->Encode(*position);
                if (!message.ok()) {
                  RCLCPP_ERROR(
                      logger, "Cannot encode position: %s", message.status().ToString().c_str());
                  return;
                }
                publisher->publish(*message);
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
  return std::make_shared<PositionPublisher>("position_feedback_test", 1, config);
}
#endif
