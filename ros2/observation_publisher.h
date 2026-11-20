#pragma once

#include <functional>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/interfaces/observation_source.h"
#include "ros2/logging.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"
#include "ros2/utils/robot_timer.h"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

namespace ros2_utils {
using ObservationPublish = std::function<absl::Status(const robot::perception::PerceptionPacket&)>;
using ObservationSnapshot = std::function<robot::perception::ObservationState::Snapshot()>;
using ObservationPublisherFactory = std::function<ObservationPublish(
    rclcpp::Node&, const ros2::node::Publisher&, const robot::perception::SinglePerception&)>;

inline ObservationPublish MakePacketPublisher(rclcpp::Node& node,
                                              const ros2::node::Publisher& output,
                                              const robot::perception::SinglePerception& sensor) {
  auto publisher = node.create_publisher<std_msgs::msg::UInt8MultiArray>(
      output.topic(), CreateQosSetting(sensor.node().qos_setting()));
  return [publisher](const robot::perception::PerceptionPacket& packet) {
    std_msgs::msg::UInt8MultiArray message;
    const auto bytes = packet.SerializeAsString();
    message.data.assign(bytes.begin(), bytes.end());
    publisher->publish(message);
    return absl::OkStatus();
  };
}

inline void PublishMetadata(const rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr& pub,
                            const robot::perception::PerceptionPacket& source) {
  robot::perception::PerceptionPacket packet;
  packet.set_perception_id(source.perception_id());
  packet.set_sequence_id(source.sequence_id());
  packet.set_timestamp(source.timestamp());
  *packet.mutable_sample_metadata() = source.sample_metadata();
  const auto bytes = packet.SerializeAsString();
  std_msgs::msg::UInt8MultiArray msg;
  msg.data.assign(bytes.begin(), bytes.end());
  pub->publish(msg);
}

// The caller owns sample state, drivers, acquisition and every returned timer.
// snapshot must be nonblocking (a state snapshot or an existing coordinator cache).
inline void AddObservationOutputs(rclcpp::Node& node,
                                  const robot::perception::SinglePerception& sensor,
                                  ObservationSnapshot snapshot,
                                  const ObservationPublisherFactory& make_publish,
                                  std::vector<rclcpp::TimerBase::SharedPtr>& timers) {
  for (const auto& config : sensor.node().publishers()) {
    auto publish = make_publish(node, config, sensor);
    auto metadata =
        node.create_publisher<std_msgs::msg::UInt8MultiArray>(config.topic() + "/metadata", 1);
    auto publish_sample = [snapshot, publish, metadata] {
      const auto sample = snapshot();
      if (!sample.packet) return;
      const bool usable = sample.health.state() == robot::perception::ObservationStatus::READY;
      if (!usable && !sample.packet->has_scalar()) return;
      if (!sample.packet->has_scalar()) {
        const auto status = publish(*sample.packet);
        if (!status.ok())
          JOSHUA_LOG(WARNING) << "Observation publication: " << status.ToString();
        else
          PublishMetadata(metadata, *sample.packet);
        return;
      }
      auto packet = *sample.packet;
      auto* s = packet.mutable_scalar();
      const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
      if (s->host_receipt_steady_ns() <= static_cast<uint64_t>(now))
        s->set_receipt_age_us((now - s->host_receipt_steady_ns()) / 1000);
      s->set_usable(s->usable() && usable);
      s->set_receipt_fresh(s->receipt_fresh() && usable);
      const auto status = publish(packet);
      if (!status.ok())
        JOSHUA_LOG(WARNING) << "Observation publication: " << status.ToString();
      else
        PublishMetadata(metadata, packet);
    };
    if (sensor.has_joshua_wire_scalar_config()) {
      timers.push_back(
          node.create_wall_timer(std::chrono::nanoseconds(1000000000ull / config.publish_rate_hz()),
                                 std::move(publish_sample)));
    } else {
      timers.push_back(
          CreateRobotTimer(node, 1.0 / config.publish_rate_hz(), std::move(publish_sample)));
    }
    auto status =
        node.create_publisher<std_msgs::msg::UInt8MultiArray>(config.topic() + "/status", 1);
    auto health = node.create_publisher<std_msgs::msg::String>(config.topic() + "/health", 1);
    timers.push_back(node.create_wall_timer(
        std::chrono::milliseconds(50),
        [snapshot,
         status,
         health,
         name = sensor.sensor_name(),
         scalar = sensor.has_joshua_wire_scalar_config()] {
          auto sample = snapshot();
          robot::perception::PerceptionPacket packet;
          packet.set_perception_id(name);
          *packet.mutable_observation_status() = sample.health;
          if (sample.packet) *packet.mutable_sample_metadata() = sample.packet->sample_metadata();
          std_msgs::msg::UInt8MultiArray message;
          const auto bytes = packet.SerializeAsString();
          message.data.assign(bytes.begin(), bytes.end());
          status->publish(message);
          std_msgs::msg::String summary;
          summary.data =
              scalar ? (sample.health.state() == robot::perception::ObservationStatus::READY
                            ? "usable"
                            : "invalid_or_stale")
                     : robot::perception::ObservationStatus::State_Name(sample.health.state());
          if (!sample.health.detail().empty()) summary.data += ": " + sample.health.detail();
          health->publish(summary);
        }));
  }
}
}  // namespace ros2_utils
