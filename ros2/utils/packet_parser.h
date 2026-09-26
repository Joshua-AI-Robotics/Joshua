#pragma once

#include <functional>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/action/proto/action.pb.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace rclcpp {
class Node;
class SubscriptionBase;
}  // namespace rclcpp

namespace ros2_utils {

// Fixed wire contracts, instantiated at compile time in packet_parser.cc.
// These checks do not construct ROS entities or open hardware.
absl::Status ValidatePositionMessageType(ros2::data_type::Ros2DataType type,
                                         const robot::perception::SinglePerception& sensor);
absl::Status ValidateActionMessageType(const ros2::node::Subscription& subscription,
                                       const robot::action::Actuator& actuator);
using PositionMessagePublisher = std::function<absl::Status(float)>;
using ActionMessageCallback = std::function<void(absl::StatusOr<robot::action::ActionPacket>)>;
absl::StatusOr<PositionMessagePublisher> CreatePositionMessagePublisher(
    rclcpp::Node& node,
    const ros2::node::Publisher& publisher,
    const robot::perception::SinglePerception& sensor);
absl::StatusOr<std::shared_ptr<rclcpp::SubscriptionBase>> CreateActionMessageSubscription(
    rclcpp::Node& node,
    const ros2::node::Subscription& subscription,
    const robot::action::SingleAction& action,
    ActionMessageCallback callback);

float MapNormalizedPosition(float value, float lower, float upper);
float DenormalizePositionValue(float value, float lower, float upper);

void DenormalizeActionPacket(robot::action::ActionPacket& packet, float lower, float upper);

absl::StatusOr<std::string> ParseActionTypeFromTopic(const std::string& topic);
absl::StatusOr<std::string> DeviceIdFromTopic(const std::string& topic);
absl::StatusOr<robot::action::ActionPacket> ActionPacketFromFloat(float value,
                                                                  const std::string& topic,
                                                                  bool normalized = false);

absl::StatusOr<float> RequirePerceptionPosition(const robot::perception::PerceptionPacket& packet);
absl::Status RequirePerceptionImage(const robot::perception::PerceptionPacket& packet);
absl::Status RequirePerceptionPointCloud(const robot::perception::PerceptionPacket& packet);

}  // namespace ros2_utils
