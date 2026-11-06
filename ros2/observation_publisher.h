#pragma once

#include <algorithm>
#include <functional>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/interfaces/observation_source.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"
#include "sensor_msgs/msg/image.hpp"
#include "sensor_msgs/msg/point_cloud2.hpp"
#include "sensor_msgs/point_cloud2_iterator.hpp"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"

namespace ros2_utils {
class ObservationPublisher {
 public:
  ObservationPublisher(rclcpp::Node& node,
                       const robot::perception::SinglePerception& sensor,
                       const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    if (sensor.node().publishers().empty()) return;
    // Validate/build every output before the factory can open any device.
    std::vector<Publish> publishers;
    for (const auto& p : sensor.node().publishers())
      publishers.push_back(MakePublisher(node, p, sensor));
    size_t publisher_index = 0;
    uint32_t rate = 1;
    for (const auto& p : sensor.node().publishers()) rate = std::max(rate, p.publish_rate_hz());
    const auto& policy = sensor.observation_policy();
    source_ = std::make_shared<robot::perception::ObservationSource>(
        [sensor, boards] {
          return robot::perception::PerceptionFactory::CreateInitializedPerception(sensor, boards);
        },
        std::chrono::nanoseconds(1000000000ull / rate),
        std::chrono::milliseconds(policy.has_stale_after_ms() ? policy.stale_after_ms() : 1000));
    for (const auto& config : sensor.node().publishers()) {
      auto publish = publishers.at(publisher_index++);
      timers_.push_back(node.create_wall_timer(
          std::chrono::nanoseconds(1000000000ull / config.publish_rate_hz()),
          [source = source_, publish, logger = node.get_logger()] {
            const auto sample = source->Get();
            if (!sample.packet) return;
            const bool usable =
                sample.health.state() == robot::perception::ObservationStatus::READY;
            if (!usable && !sample.packet->has_scalar()) return;
            if (!sample.packet->has_scalar()) {
              const auto status = publish(*sample.packet);
              if (!status.ok())
                RCLCPP_WARN(logger, "Observation publication: %s", status.ToString().c_str());
              return;
            }
            auto packet = *sample.packet;
            if (packet.has_scalar()) {
              auto* s = packet.mutable_scalar();
              const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                   std::chrono::steady_clock::now().time_since_epoch())
                                   .count();
              if (s->host_receipt_steady_ns() <= static_cast<uint64_t>(now))
                s->set_receipt_age_us((now - s->host_receipt_steady_ns()) / 1000);
              s->set_usable(s->usable() && usable);
              s->set_receipt_fresh(s->receipt_fresh() && usable);
            }
            const auto status = publish(packet);
            if (!status.ok())
              RCLCPP_WARN(logger, "Observation publication: %s", status.ToString().c_str());
          }));
      auto status =
          node.create_publisher<std_msgs::msg::UInt8MultiArray>(config.topic() + "/status", 1);
      auto health = node.create_publisher<std_msgs::msg::String>(config.topic() + "/health", 1);
      timers_.push_back(node.create_wall_timer(
          std::chrono::milliseconds(50),
          [source = source_,
           status,
           health,
           name = sensor.sensor_name(),
           scalar = sensor.has_joshua_wire_scalar_config()] {
            auto snapshot = source->Get();
            robot::perception::PerceptionPacket packet;
            packet.set_perception_id(name);
            *packet.mutable_observation_status() = snapshot.health;
            std_msgs::msg::UInt8MultiArray message;
            const auto bytes = packet.SerializeAsString();
            message.data.assign(bytes.begin(), bytes.end());
            status->publish(message);
            std_msgs::msg::String summary;
            summary.data =
                scalar ? (snapshot.health.state() == robot::perception::ObservationStatus::READY
                              ? "usable"
                              : "invalid_or_stale")
                       : robot::perception::ObservationStatus::State_Name(snapshot.health.state());
            if (!snapshot.health.detail().empty()) summary.data += ": " + snapshot.health.detail();
            health->publish(summary);
          }));
    }
  }
  ~ObservationPublisher() {
    timers_.clear();
    if (source_) source_->Stop();
  }

 private:
  using Publish = std::function<absl::Status(const robot::perception::PerceptionPacket&)>;
  static Publish MakePublisher(rclcpp::Node& node,
                               const ros2::node::Publisher& config,
                               const robot::perception::SinglePerception& sensor) {
    const auto qos = CreateQosSetting(sensor.node().qos_setting());
    if (sensor.sensor_type() == robot::perception::POSITION) {
      auto made = CreatePositionMessagePublisher(node, config, sensor);
      if (!made.ok()) throw std::runtime_error(made.status().ToString());
      return [publish = *made](const auto& packet) {
        auto value = RequirePerceptionPosition(packet);
        return value.ok() ? publish(*value) : value.status();
      };
    }
    if (sensor.sensor_type() == robot::perception::SCALAR &&
        config.ros2_data_type() == ros2::data_type::UINT8_MULTI_ARRAY) {
      auto publisher = node.create_publisher<std_msgs::msg::UInt8MultiArray>(config.topic(), qos);
      return [publisher](const auto& packet) {
        std_msgs::msg::UInt8MultiArray message;
        const auto bytes = packet.SerializeAsString();
        message.data.assign(bytes.begin(), bytes.end());
        publisher->publish(message);
        return absl::OkStatus();
      };
    }
    if (sensor.sensor_type() == robot::perception::IMAGE &&
        config.ros2_data_type() == ros2::data_type::IMAGE) {
      auto publisher = node.create_publisher<sensor_msgs::msg::Image>(config.topic(), qos);
      return [publisher, &node, name = sensor.sensor_name()](const auto& packet) {
        const auto& image = packet.image();
        sensor_msgs::msg::Image msg;
        msg.header.stamp = node.now();
        msg.header.frame_id = "camera_frame";
        msg.width = image.width();
        msg.height = image.height();
        msg.encoding = "rgb8";
        msg.step = msg.width * 3;
        msg.data.assign(image.data().begin(), image.data().end());
        for (size_t i = 0; i < msg.data.size(); i += 3) std::swap(msg.data[i], msg.data[i + 2]);
        publisher->publish(msg);
        return absl::OkStatus();
      };
    }
    if (sensor.sensor_type() == robot::perception::RANGE_SCAN &&
        config.ros2_data_type() == ros2::data_type::POINTCLOUD2) {
      auto publisher = node.create_publisher<sensor_msgs::msg::PointCloud2>(config.topic(), qos);
      return [publisher, &node, name = sensor.sensor_name()](const auto& packet) {
        const auto& cloud = packet.point_cloud();
        sensor_msgs::msg::PointCloud2 msg;
        msg.header.stamp = node.now();
        msg.header.frame_id = cloud.frame_id().empty() ? name : cloud.frame_id();
        msg.height = 1;
        msg.width = cloud.x_size();
        msg.is_dense = cloud.is_dense();
        sensor_msgs::PointCloud2Modifier modifier(msg);
        modifier.setPointCloud2Fields(4,
                                      "x",
                                      1,
                                      sensor_msgs::msg::PointField::FLOAT32,
                                      "y",
                                      1,
                                      sensor_msgs::msg::PointField::FLOAT32,
                                      "z",
                                      1,
                                      sensor_msgs::msg::PointField::FLOAT32,
                                      "intensity",
                                      1,
                                      sensor_msgs::msg::PointField::FLOAT32);
        modifier.resize(cloud.x_size());
        sensor_msgs::PointCloud2Iterator<float> x(msg, "x"), y(msg, "y"), z(msg, "z"),
            intensity(msg, "intensity");
        for (int i = 0; i < cloud.x_size(); ++i, ++x, ++y, ++z, ++intensity) {
          *x = cloud.x(i);
          *y = cloud.y(i);
          *z = cloud.z(i);
          *intensity = cloud.intensity_size() ? cloud.intensity(i) : 0;
        }
        publisher->publish(msg);
        return absl::OkStatus();
      };
    }
    throw std::invalid_argument("Unsupported observation publisher/type");
  }
  std::shared_ptr<robot::perception::ObservationSource> source_;
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;
};

class ObservationNode : public rclcpp::Node {
 public:
  ObservationNode(const std::string& name, int id, const config::Config& config) : Node(name) {
    for (const auto& sensor : config.robot().perceptions().single_perceptions())
      if (sensor.node().id() == static_cast<uint32_t>(id))
        sources_.push_back(
            std::make_unique<ObservationPublisher>(*this, sensor, config.robot().boards()));
  }

 private:
  std::vector<std::unique_ptr<ObservationPublisher>> sources_;
};
}  // namespace ros2_utils
