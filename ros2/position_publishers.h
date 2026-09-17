#pragma once

#include <chrono>
#include <memory>
#include <vector>

#include "config/proto/robot.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/float32.hpp"

namespace ros2_utils {

// Reads and publishes the position sensors assigned to a standalone position node.
class PositionPublishers {
 public:
  PositionPublishers(rclcpp::Node& node, uint32_t node_id, const config::Robot& robot) {
    for (const auto& sensor : robot.perceptions().single_perceptions()) {
      if (sensor.sensor_type() != robot::perception::POSITION || sensor.node().id() != node_id) {
        continue;
      }
      auto result = robot::perception::PerceptionFactory::CreatePerception(sensor, robot.boards());
      if (!result.ok()) {
        RCLCPP_ERROR(node.get_logger(),
                     "Failed to create position sensor '%s': %s",
                     sensor.sensor_name().c_str(),
                     result.status().ToString().c_str());
        continue;
      }
      auto interface = std::shared_ptr<robot::perception::PerceptionInterface>(std::move(*result));
      for (const auto& config : sensor.node().publishers()) {
        // TODO(hmoon): Support additional position message types, including Float64
        // (double), JointState, and other configured ROS message types.
        if (config.ros2_data_type() != ros2::data_type::FLOAT32 || config.publish_rate_hz() == 0) {
          RCLCPP_ERROR(node.get_logger(),
                       "Position topic '%s' requires FLOAT32 and a positive rate.",
                       config.topic().c_str());
          continue;
        }
        auto publisher = node.create_publisher<std_msgs::msg::Float32>(
            config.topic(), CreateQosSetting(sensor.node().qos_setting()));
        // Each timer reads only its sensor, so adding sensors does not multiply
        // every sensor's read/publish rate.
        timers_.push_back(node.create_wall_timer(
            std::chrono::duration<double>(1.0 / config.publish_rate_hz()),
            [interface, publisher, logger = node.get_logger()]() {
              try {
                auto packet = interface->GetData();
                if (!packet.ok()) {
                  RCLCPP_WARN(logger,
                              "Failed to read position sensor '%s': %s",
                              interface->GetId().c_str(),
                              packet.status().ToString().c_str());
                  return;
                }
                auto position = RequirePerceptionPosition(*packet);
                if (!position.ok()) {
                  RCLCPP_WARN(
                      logger, "Invalid position packet from '%s'", interface->GetId().c_str());
                  return;
                }
                std_msgs::msg::Float32 message;
                message.data = *position;
                publisher->publish(message);
              } catch (const std::exception& e) {
                RCLCPP_ERROR(logger, "Error publishing position: %s", e.what());
              }
            }));
      }
    }
  }

 private:
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;
};
}  // namespace ros2_utils
