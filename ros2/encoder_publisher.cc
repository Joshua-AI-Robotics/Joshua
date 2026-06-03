#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/perception/factory/perception_factory.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "ros2/node_runner.h"
#include "ros2/proto/node.pb.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/u_int8_multi_array.hpp"

namespace {

ros2::node::PayloadType EffectivePayloadType(const ros2::node::PayloadType payload_type) {
  if (payload_type == ros2::node::PAYLOAD_TYPE_UNSPECIFIED) {
    return ros2::node::PAYLOAD_TYPE_PERCEPTION_PACKET;
  }
  return payload_type;
}

bool ValidateEncoderPublisher(const ros2::data_type::Ros2DataType ros2_data_type,
                              const ros2::node::PayloadType payload_type) {
  if (ros2_data_type != ros2::data_type::UINT8_MULTI_ARRAY) {
    return false;
  }
  const auto resolved = EffectivePayloadType(payload_type);
  return resolved == ros2::node::PAYLOAD_TYPE_PERCEPTION_PACKET;
}

}  // namespace

class EncoderPublisher : public rclcpp::Node {
 private:
  struct Encoder {
    std::string topic;
    std::shared_ptr<robot::perception::PerceptionInterface> interface;
    rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr publisher;
    rclcpp::TimerBase::SharedPtr timer;
  };

 public:
  EncoderPublisher(const std::string& node_name, const int node_id, const config::Config& config)
      : Node(node_name) {
    for (const auto& single_perception : config.robot().perceptions().single_perceptions()) {
      if (single_perception.perception_type() != robot::perception::PerceptionType::ENCODER ||
          static_cast<int>(single_perception.node().id()) != node_id) {
        continue;
      }

      const auto& encoder_proto = single_perception.encoder();
      const auto& qos_setting = single_perception.node().qos_setting();

      auto interface = robot::perception::PerceptionFactory::CreatePerception(single_perception);
      if (!interface.ok()) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to create perception interface for encoder '%s'. Check hardware "
                     "connection or permissions.",
                     encoder_proto.encoder_name().c_str());
        continue;
      }

      auto shared_interface =
          std::shared_ptr<robot::perception::PerceptionInterface>(std::move(interface.value()));

      for (const auto& publisher : single_perception.node().publishers()) {
        if (!ValidateEncoderPublisher(publisher.ros2_data_type(), publisher.payload_type())) {
          RCLCPP_ERROR(this->get_logger(),
                       "Invalid publisher config for encoder topic '%s' "
                       "(ros2_data_type=%d, payload_type=%d). "
                       "Require UINT8_MULTI_ARRAY and PERCEPTION_PACKET.",
                       publisher.topic().c_str(),
                       static_cast<int>(publisher.ros2_data_type()),
                       static_cast<int>(publisher.payload_type()));
          continue;
        }

        encoders_.emplace_back(
            Encoder{.topic = publisher.topic(),
                    .interface = shared_interface,
                    .publisher = this->create_publisher<std_msgs::msg::UInt8MultiArray>(
                        publisher.topic(), ros2_utils::CreateQosSetting(qos_setting)),
                    .timer = this->create_wall_timer(
                        std::chrono::milliseconds(1000 / publisher.publish_rate_hz()),
                        [this]() { publish_encoder_data(); })});
      }

      RCLCPP_INFO(this->get_logger(),
                  "Found encoder '%s' in configuration for node_id %d. Publishing on %zu topics.",
                  encoder_proto.encoder_name().c_str(),
                  node_id,
                  single_perception.node().publishers().size());
    }

    if (encoders_.empty()) {
      RCLCPP_ERROR(
          this->get_logger(), "No encoders found in configuration for node_id %d!", node_id);
      return;
    }

    RCLCPP_INFO(this->get_logger(),
                "Encoder publisher node started with %zu encoders for node_id %d!",
                encoders_.size(),
                node_id);
  }

 private:
  void publish_encoder_data() {
    if (encoders_.empty()) {
      RCLCPP_WARN(this->get_logger(), "No encoders initialized, skipping publish cycle.");
      return;
    }

    try {
      for (auto& encoder : encoders_) {
        auto packet = encoder.interface->GetData();

        if (!packet.ok()) {
          RCLCPP_WARN(
              this->get_logger(), "Failed to get data from encoder '%s'!", encoder.topic.c_str());
          continue;
        }

        if (!ros2_utils::RequirePerceptionPosition(packet.value()).ok()) {
          RCLCPP_WARN(this->get_logger(),
                      "Failed to get position data from encoder '%s'!",
                      encoder.topic.c_str());
          continue;
        }

        const auto serialized = ros2_utils::SerializePerceptionPacket(packet.value());
        auto message = std_msgs::msg::UInt8MultiArray();
        message.data.assign(serialized.begin(), serialized.end());
        encoder.publisher->publish(message);
      }
    } catch (const std::exception& e) {
      RCLCPP_ERROR(this->get_logger(), "Error publishing encoder data: %s", e.what());
    }
  }

  std::vector<Encoder> encoders_;
};

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<EncoderPublisher>(argc, argv, "encoder_publisher");
}
