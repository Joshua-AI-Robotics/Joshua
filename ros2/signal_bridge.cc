#include <algorithm>
#include <list>
#include <map>
#include <string>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "ros2/node_runner.h"
#include "ros2/proto/bridge.pb.h"
#include "ros2/proto/node.pb.h"
#include "ros2/proto/ros2_data_type.pb.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/u_int8_multi_array.hpp"

class SignalBridge : public rclcpp::Node {
 private:
  struct MappingRuntime {
    std::string mapping_id;
    std::string source_topic;
    std::string target_topic;
    ros2::bridge::PayloadType source_payload_type;
    ros2::bridge::MappingType mapping_type;
    ros2::bridge::AffineTransform affine;
    std::string action_id;
    rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr publisher;
    rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr subscription;
  };

 public:
  SignalBridge(const std::string& node_name, const int node_id, const config::Config& config)
      : Node(node_name) {
    for (const auto& single_bridge : config.robot().bridges().single_bridges()) {
      if (static_cast<int>(single_bridge.node().id()) != node_id ||
          single_bridge.node().node_type() != ros2::node::NodeType::SIGNAL_BRIDGE) {
        continue;
      }

      const auto qos = ros2_utils::CreateQosSetting(single_bridge.node().qos_setting());
      ValidateNodeTopics(single_bridge.node());

      for (const auto& mapping : single_bridge.bridge().mappings()) {
        if (mapping.source_topic().empty() || mapping.target_topic().empty()) {
          RCLCPP_ERROR(this->get_logger(),
                       "Mapping '%s' must define source_topic and target_topic.",
                       mapping.mapping_id().c_str());
          continue;
        }

        auto publisher_it = publishers_.find(mapping.target_topic());
        if (publisher_it == publishers_.end()) {
          auto publisher =
              this->create_publisher<std_msgs::msg::UInt8MultiArray>(mapping.target_topic(), qos);
          publisher_it = publishers_.emplace(mapping.target_topic(), publisher).first;
        }

        auto& runtime = mappings_.emplace_back(MappingRuntime{
            .mapping_id = mapping.mapping_id().empty()
                              ? mapping.source_topic() + "->" + mapping.target_topic()
                              : mapping.mapping_id(),
            .source_topic = mapping.source_topic(),
            .target_topic = mapping.target_topic(),
            .source_payload_type = mapping.source_payload_type(),
            .mapping_type = mapping.mapping_type(),
            .affine = mapping.affine(),
            .action_id = mapping.action_id(),
            .publisher = publisher_it->second,
        });

        runtime.subscription = this->create_subscription<std_msgs::msg::UInt8MultiArray>(
            runtime.source_topic,
            qos,
            [this, &runtime](const std_msgs::msg::UInt8MultiArray::ConstSharedPtr msg) {
              ProcessMessage(runtime, *msg);
            });
      }
    }

    if (mappings_.empty()) {
      RCLCPP_ERROR(this->get_logger(), "No signal bridge mappings found for node_id %d!", node_id);
      return;
    }

    RCLCPP_INFO(this->get_logger(),
                "Signal bridge started with %zu mapping(s) for node_id %d.",
                mappings_.size(),
                node_id);
  }

 private:
  std::list<MappingRuntime> mappings_;
  std::map<std::string, rclcpp::Publisher<std_msgs::msg::UInt8MultiArray>::SharedPtr> publishers_;

  void ValidateNodeTopics(const ros2::node::Node& node_cfg) {
    for (const auto& sub : node_cfg.subscriptions()) {
      if (sub.ros2_data_type() != ros2::data_type::UINT8_MULTI_ARRAY) {
        RCLCPP_ERROR(this->get_logger(),
                     "Unsupported subscription type %d on topic '%s'. "
                     "Signal bridge requires UINT8_MULTI_ARRAY.",
                     static_cast<int>(sub.ros2_data_type()),
                     sub.topic().c_str());
      }
    }
    for (const auto& pub : node_cfg.publishers()) {
      if (pub.ros2_data_type() != ros2::data_type::UINT8_MULTI_ARRAY) {
        RCLCPP_ERROR(this->get_logger(),
                     "Unsupported publisher type %d on topic '%s'. "
                     "Signal bridge requires UINT8_MULTI_ARRAY.",
                     static_cast<int>(pub.ros2_data_type()),
                     pub.topic().c_str());
      }
    }
  }

  void ProcessMessage(const MappingRuntime& runtime, const std_msgs::msg::UInt8MultiArray& msg) {
    robot::action::ActionPacket action;
    if (!ToActionPacket(runtime, msg, action)) {
      return;
    }
    auto serialized = action.SerializeAsString();
    std_msgs::msg::UInt8MultiArray out;
    out.data.assign(serialized.begin(), serialized.end());
    runtime.publisher->publish(out);
  }

  bool ToActionPacket(const MappingRuntime& runtime,
                      const std_msgs::msg::UInt8MultiArray& msg,
                      robot::action::ActionPacket& out) {
    if (runtime.source_payload_type == ros2::bridge::PAYLOAD_TYPE_ACTION_PACKET) {
      if (!out.ParseFromArray(msg.data.data(), static_cast<int>(msg.data.size()))) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to parse ActionPacket for mapping '%s'.",
                     runtime.mapping_id.c_str());
        return false;
      }
      if (runtime.mapping_type != ros2::bridge::MAPPING_TYPE_PASS_THROUGH_ACTION &&
          runtime.mapping_type != ros2::bridge::MAPPING_TYPE_INVALID) {
        RCLCPP_WARN(this->get_logger(),
                    "Mapping '%s' uses ACTION_PACKET source with mapping_type %d. "
                    "Pass-through is applied.",
                    runtime.mapping_id.c_str(),
                    static_cast<int>(runtime.mapping_type));
      }
      if (!runtime.action_id.empty()) {
        out.set_action_id(runtime.action_id);
      }
      return true;
    }

    if (runtime.source_payload_type == ros2::bridge::PAYLOAD_TYPE_PERCEPTION_PACKET) {
      robot::perception::PerceptionPacket perception;
      if (!perception.ParseFromArray(msg.data.data(), static_cast<int>(msg.data.size()))) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to parse PerceptionPacket for mapping '%s'.",
                     runtime.mapping_id.c_str());
        return false;
      }
      if (runtime.mapping_type != ros2::bridge::MAPPING_TYPE_POSITION_TO_POSITION) {
        RCLCPP_ERROR(this->get_logger(),
                     "Unsupported mapping_type %d for PERCEPTION_PACKET in mapping '%s'.",
                     static_cast<int>(runtime.mapping_type),
                     runtime.mapping_id.c_str());
        return false;
      }
      if (!perception.has_position()) {
        RCLCPP_WARN(this->get_logger(),
                    "PerceptionPacket in mapping '%s' has no position field.",
                    runtime.mapping_id.c_str());
        return false;
      }

      float value = perception.position().position();
      if (runtime.affine.invert()) {
        value = -value;
      }
      float scale = 1.0f;
      float offset = 0.0f;
      if (runtime.affine.ByteSizeLong() > 0) {
        scale = runtime.affine.scale();
        offset = runtime.affine.offset();
      }
      value = value * scale + offset;
      if (runtime.affine.clamp_enabled()) {
        value = std::max(runtime.affine.clamp_min(), std::min(runtime.affine.clamp_max(), value));
      }

      out.Clear();
      out.set_position(value);
      out.set_timestamp_ns(perception.timestamp_ns() != 0
                               ? perception.timestamp_ns()
                               : static_cast<int64_t>(this->get_clock()->now().nanoseconds()));
      out.set_action_id(runtime.action_id.empty() ? runtime.mapping_id : runtime.action_id);
      return true;
    }

    RCLCPP_ERROR(this->get_logger(),
                 "Unsupported source_payload_type %d in mapping '%s'.",
                 static_cast<int>(runtime.source_payload_type),
                 runtime.mapping_id.c_str());
    return false;
  }
};

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<SignalBridge>(argc, argv, "signal_bridge");
}
