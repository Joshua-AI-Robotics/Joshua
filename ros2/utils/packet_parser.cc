#include "ros2/utils/packet_parser.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <type_traits>
#include <vector>

#include "absl/strings/str_join.h"
#include "rclcpp/rclcpp.hpp"
#include "ros2/utils/qos_setting.h"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/byte.hpp"
#include "std_msgs/msg/char.hpp"
#include "std_msgs/msg/float32.hpp"
#include "std_msgs/msg/float32_multi_array.hpp"
#include "std_msgs/msg/float64.hpp"
#include "std_msgs/msg/float64_multi_array.hpp"
#include "std_msgs/msg/int16.hpp"
#include "std_msgs/msg/int16_multi_array.hpp"
#include "std_msgs/msg/int32.hpp"
#include "std_msgs/msg/int32_multi_array.hpp"
#include "std_msgs/msg/int64.hpp"
#include "std_msgs/msg/int64_multi_array.hpp"
#include "std_msgs/msg/int8.hpp"
#include "std_msgs/msg/int8_multi_array.hpp"
#include "std_msgs/msg/u_int16.hpp"
#include "std_msgs/msg/u_int16_multi_array.hpp"
#include "std_msgs/msg/u_int32.hpp"
#include "std_msgs/msg/u_int32_multi_array.hpp"
#include "std_msgs/msg/u_int64.hpp"
#include "std_msgs/msg/u_int64_multi_array.hpp"
#include "std_msgs/msg/u_int8.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"
#include "utils/status_macros.h"

namespace ros2_utils {
namespace {

using robot::action::ActionPacket;
// STS3215 encoder resolution is a device property (4096 ticks/revolution).
// JointState uses radians; scalar messages keep the existing native units.
constexpr double kPi = 3.14159265358979323846;
constexpr double kStsTicksPerRadian = 4096.0 / (2.0 * kPi);

template <typename T>
struct MessageTag {
  using Type = T;
};
template <typename Fn>
absl::Status VisitPositionMessage(ros2::data_type::Ros2DataType type, Fn fn) {
  switch (type) {
    case ros2::data_type::FLOAT32:
      return fn(MessageTag<std_msgs::msg::Float32>{});
    case ros2::data_type::FLOAT64:
      return fn(MessageTag<std_msgs::msg::Float64>{});
    case ros2::data_type::INT8:
      return fn(MessageTag<std_msgs::msg::Int8>{});
    case ros2::data_type::INT16:
      return fn(MessageTag<std_msgs::msg::Int16>{});
    case ros2::data_type::INT32:
      return fn(MessageTag<std_msgs::msg::Int32>{});
    case ros2::data_type::INT64:
      return fn(MessageTag<std_msgs::msg::Int64>{});
    case ros2::data_type::UINT8:
      return fn(MessageTag<std_msgs::msg::UInt8>{});
    case ros2::data_type::UINT16:
      return fn(MessageTag<std_msgs::msg::UInt16>{});
    case ros2::data_type::UINT32:
      return fn(MessageTag<std_msgs::msg::UInt32>{});
    case ros2::data_type::UINT64:
      return fn(MessageTag<std_msgs::msg::UInt64>{});
    case ros2::data_type::BYTE:
      return fn(MessageTag<std_msgs::msg::Byte>{});
    case ros2::data_type::CHAR:
      return fn(MessageTag<std_msgs::msg::Char>{});
    case ros2::data_type::FLOAT32_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::Float32MultiArray>{});
    case ros2::data_type::FLOAT64_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::Float64MultiArray>{});
    case ros2::data_type::INT8_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::Int8MultiArray>{});
    case ros2::data_type::INT16_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::Int16MultiArray>{});
    case ros2::data_type::INT32_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::Int32MultiArray>{});
    case ros2::data_type::INT64_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::Int64MultiArray>{});
    case ros2::data_type::UINT8_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::UInt8MultiArray>{});
    case ros2::data_type::UINT16_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::UInt16MultiArray>{});
    case ros2::data_type::UINT32_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::UInt32MultiArray>{});
    case ros2::data_type::UINT64_MULTI_ARRAY:
      return fn(MessageTag<std_msgs::msg::UInt64MultiArray>{});
    case ros2::data_type::BOOL:
      return fn(MessageTag<std_msgs::msg::Bool>{});
    case ros2::data_type::JOINT_STATE:
      return fn(MessageTag<sensor_msgs::msg::JointState>{});
    default:
      return absl::InvalidArgumentError("Unsupported position/actuator ROS message type");
  }
}
template <typename T>
struct IsVector : std::false_type {};
template <typename T, typename A>
struct IsVector<std::vector<T, A>> : std::true_type {};

// Validate before narrowing: floating-to-integer overflow is undefined, and
// truncating a fractional position to a tick should never be implicit.
template <typename T>
absl::StatusOr<T> CheckedNumber(long double value) {
  if (!std::isfinite(value)) return absl::InvalidArgumentError("Non-finite command/position");
  if constexpr (std::is_integral_v<T>) {
    const auto bound = std::ldexp(1.0L, std::numeric_limits<T>::digits);
    if (value < (std::is_signed_v<T> ? -bound : 0.0L) || value >= bound ||
        std::trunc(value) != value)
      return absl::OutOfRangeError("Position is fractional or outside integer message range");
  } else if (value < -std::numeric_limits<T>::max() || value > std::numeric_limits<T>::max()) {
    return absl::OutOfRangeError("Value exceeds floating-point range");
  }
  T result = static_cast<T>(value);
  if constexpr (std::is_floating_point_v<T>) {
    if (value != 0 && result == 0) return absl::OutOfRangeError("Floating-point underflow");
  }
  return result;
}
template <typename T>
absl::StatusOr<float> NativeCommand(T value) {
  ABSL_ASSIGN_OR_RETURN(auto result, CheckedNumber<float>(value));
  if constexpr (std::is_integral_v<T>) {
    if (static_cast<long double>(result) != static_cast<long double>(value))
      return absl::OutOfRangeError("Integer command loses precision in the float driver API");
  }
  return result;
}
absl::StatusOr<double> NativeUnitsPerRadian(robot::action::MotorType motor) {
  switch (motor) {
    case robot::action::MOTOR_STS3215:
      return kStsTicksPerRadian;
    case robot::action::MOTOR_STEPPER_NEMA17:
      return 180.0 / kPi;
    default:
      return absl::InvalidArgumentError("Driver has no JointState position unit conversion");
  }
}

template <typename Message>
absl::Status EncodePosition(float value,
                            const std::string& name,
                            rclcpp::Node& node,
                            Message& message) {
  if constexpr (std::is_same_v<Message, sensor_msgs::msg::JointState>) {
    ABSL_ASSIGN_OR_RETURN(auto radians, CheckedNumber<double>(value / kStsTicksPerRadian));
    message.name = {name};
    message.position = {radians};
    message.header.stamp = node.now();
    // Do not fabricate velocity or effort measurements.
  } else if constexpr (IsVector<decltype(message.data)>::value) {
    ABSL_ASSIGN_OR_RETURN(auto converted,
                          CheckedNumber<typename decltype(message.data)::value_type>(value));
    message.data = {converted};
  } else {
    ABSL_ASSIGN_OR_RETURN(message.data, CheckedNumber<decltype(message.data)>(value));
  }
  return absl::OkStatus();
}

template <typename Message>
absl::StatusOr<float> DecodeCommand(const Message& message,
                                    const robot::action::Actuator& actuator) {
  if constexpr (std::is_same_v<Message, sensor_msgs::msg::JointState>) {
    if (message.position.size() != message.name.size() || !message.velocity.empty() ||
        !message.effort.empty())
      return absl::InvalidArgumentError(
          "JointState commands require matching names/positions and empty velocity/effort");
    const auto& name = actuator.actuator_name();
    auto it = std::find(message.name.begin(), message.name.end(), name);
    if (it == message.name.end() || std::count(message.name.begin(), message.name.end(), name) != 1)
      return absl::InvalidArgumentError("JointState must contain the actuator name exactly once");
    ABSL_ASSIGN_OR_RETURN(auto scale, NativeUnitsPerRadian(actuator.motor_type()));
    return CheckedNumber<float>(
        static_cast<long double>(message.position[it - message.name.begin()]) * scale);
  } else if constexpr (IsVector<decltype(message.data)>::value) {
    if (message.data.size() != 1 || message.layout.data_offset != 0 ||
        message.layout.dim.size() > 1 ||
        (message.layout.dim.size() == 1 &&
         (message.layout.dim[0].size != 1 || message.layout.dim[0].stride != 1)))
      return absl::InvalidArgumentError(
          "Actuator MultiArray must contain exactly one contiguous value");
    return NativeCommand(message.data.front());
  } else {
    return NativeCommand(message.data);
  }
}

template <typename Fn>
void ApplyToPositionSources(ActionPacket& packet, Fn transform) {
  if (packet.has_position()) {
    packet.set_position(transform(packet.position()));
  }
  if (packet.has_complex() && packet.complex().has_position()) {
    packet.mutable_complex()->set_position(transform(packet.complex().position()));
  }
}

// Allowed /<device_id>/<action_type> topic suffixes for numeric scalar/array actuator commands.
// TODO(hmoon): Keep in sync with ACTION_TOPIC_SUFFIX_TO_FIELD in packet_parser.py and
// ACTION_SCALAR_ONEOF_FIELDS when action_packet.proto gains a new float oneof arm.
// Checklist: ros2/utils/packet_parser.md § "After editing action_packet.proto".
const char* kActionTopicSuffixes[] = {"position", "torque", "speed", "dc"};

std::string NormalizeTopicSuffix(std::string suffix) {
  std::transform(suffix.begin(), suffix.end(), suffix.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return suffix;
}

absl::StatusOr<std::string> ActionFieldFromSuffix(const std::string& suffix) {
  const std::string normalized = NormalizeTopicSuffix(suffix);
  for (const char* allowed : kActionTopicSuffixes) {
    if (normalized == allowed) {
      return normalized;
    }
  }
  return absl::InvalidArgumentError("Unknown action_type '" + suffix + "' (expected suffix: " +
                                    absl::StrJoin(kActionTopicSuffixes, ", ") + ")");
}

}  // namespace

absl::Status ValidatePositionMessageType(ros2::data_type::Ros2DataType type,
                                         const robot::perception::SinglePerception& sensor) {
  ABSL_RETURN_IF_ERROR(VisitPositionMessage(type, [](auto) { return absl::OkStatus(); }));
  if (type == ros2::data_type::BOOL)
    return absl::InvalidArgumentError("Bool cannot represent position feedback");
  if (type == ros2::data_type::JOINT_STATE && !sensor.has_sts3215_encoder_config())
    return absl::InvalidArgumentError("Sensor has no JointState position unit conversion");
  return absl::OkStatus();
}
absl::Status ValidateActionMessageType(const ros2::node::Subscription& subscription,
                                       const robot::action::Actuator& actuator) {
  auto type = subscription.ros2_data_type();
  ABSL_RETURN_IF_ERROR(VisitPositionMessage(type, [](auto) { return absl::OkStatus(); }));
  if (type == ros2::data_type::JOINT_STATE) {
    if (subscription.normalized() || actuator.actuator_name().empty())
      return absl::InvalidArgumentError(
          "JointState requires an actuator name and unnormalized radians");
    return NativeUnitsPerRadian(actuator.motor_type()).status();
  }
  ABSL_ASSIGN_OR_RETURN(auto device, DeviceIdFromTopic(subscription.topic()));
  if (device != actuator.actuator_name())
    return absl::InvalidArgumentError("Topic device must match actuator_name");
  ABSL_ASSIGN_OR_RETURN(auto command, ParseActionTypeFromTopic(subscription.topic()));
  if (command == "dc")
    return absl::InvalidArgumentError("Current motor drivers do not support duty cycle");
  if (subscription.normalized() && command != "position")
    return absl::InvalidArgumentError("Only position commands support normalization");
  if (type == ros2::data_type::BOOL &&
      (command != "torque" || (actuator.motor_type() != robot::action::MOTOR_STS3215 &&
                               actuator.motor_type() != robot::action::MOTOR_STEPPER_NEMA17)))
    return absl::InvalidArgumentError("Bool is only supported for a driver's torque-enable gate");
  return absl::OkStatus();
}
absl::StatusOr<PositionMessagePublisher> CreatePositionMessagePublisher(
    rclcpp::Node& node,
    const ros2::node::Publisher& config,
    const robot::perception::SinglePerception& sensor) {
  ABSL_RETURN_IF_ERROR(ValidatePositionMessageType(config.ros2_data_type(), sensor));
  PositionMessagePublisher result;
  ABSL_RETURN_IF_ERROR(VisitPositionMessage(config.ros2_data_type(), [&](auto tag) {
    using Message = typename decltype(tag)::Type;
    auto publisher = node.create_publisher<Message>(config.topic(),
                                                    CreateQosSetting(sensor.node().qos_setting()));
    result = [publisher, &node, name = sensor.sensor_name()](float value) {
      Message message;
      ABSL_RETURN_IF_ERROR(EncodePosition(value, name, node, message));
      publisher->publish(message);
      return absl::OkStatus();
    };
    return absl::OkStatus();
  }));
  return result;
}
absl::StatusOr<std::shared_ptr<rclcpp::SubscriptionBase>> CreateActionMessageSubscription(
    rclcpp::Node& node,
    const ros2::node::Subscription& config,
    const robot::action::SingleAction& action,
    ActionMessageCallback callback) {
  ABSL_RETURN_IF_ERROR(ValidateActionMessageType(config, action.actuator()));
  std::shared_ptr<rclcpp::SubscriptionBase> result;
  const auto command_topic = config.ros2_data_type() == ros2::data_type::JOINT_STATE
                                 ? action.actuator().actuator_name() + "/position"
                                 : config.topic();
  ABSL_RETURN_IF_ERROR(VisitPositionMessage(config.ros2_data_type(), [&](auto tag) {
    using Message = typename decltype(tag)::Type;
    result = node.create_subscription<Message>(
        config.topic(),
        CreateQosSetting(action.node().qos_setting()),
        [actuator = action.actuator(), normalized = config.normalized(), command_topic, callback](
            typename Message::ConstSharedPtr message) {
          auto value = DecodeCommand(*message, actuator);
          if (!value.ok()) {
            callback(value.status());
            return;
          }
          callback(ActionPacketFromFloat(*value, command_topic, normalized));
        });
    return absl::OkStatus();
  }));
  return result;
}

float MapNormalizedPosition(const float value, const float lower, const float upper) {
  const float normalized = std::max(-1.0f, std::min(1.0f, value));
  return lower + (normalized + 1.0f) * (upper - lower) / 2.0f;
}

float DenormalizePositionValue(const float value, const float lower, const float upper) {
  const float position = MapNormalizedPosition(value, lower, upper);
  return std::max(lower, std::min(upper, position));
}

void DenormalizeActionPacket(ActionPacket& packet, const float lower, const float upper) {
  if (!packet.normalized()) {
    return;
  }
  ApplyToPositionSources(packet, [lower, upper](const float value) {
    return DenormalizePositionValue(value, lower, upper);
  });
}

absl::StatusOr<std::string> ParseActionTypeFromTopic(const std::string& topic) {
  const auto slash = topic.find_last_of('/');
  if (slash == std::string::npos || slash + 1 >= topic.size()) {
    return absl::InvalidArgumentError("Actuator topic '" + topic +
                                      "' must be /<device_id>/<action_type>");
  }
  return ActionFieldFromSuffix(topic.substr(slash + 1));
}

absl::StatusOr<std::string> DeviceIdFromTopic(const std::string& topic) {
  std::string trimmed = topic;
  while (!trimmed.empty() && trimmed.front() == '/') {
    trimmed.erase(trimmed.begin());
  }
  const auto slash = trimmed.find_last_of('/');
  if (slash == std::string::npos || slash == 0) {
    return absl::InvalidArgumentError("Actuator topic '" + topic +
                                      "' must be /<device_id>/<action_type>");
  }
  return trimmed.substr(0, slash);
}

absl::StatusOr<ActionPacket> ActionPacketFromFloat(const float value,
                                                   const std::string& topic,
                                                   const bool normalized) {
  auto field = ParseActionTypeFromTopic(topic);
  if (!field.ok()) {
    return field.status();
  }

  ActionPacket packet;
  if (field.value() == "position") {
    packet.set_normalized(normalized);
    packet.set_position(value);
  } else if (field.value() == "torque") {
    packet.set_torque(value);
  } else if (field.value() == "speed") {
    packet.set_speed(value);
  } else if (field.value() == "dc") {
    packet.set_dc(value);
  }
  return packet;
}

absl::StatusOr<float> RequirePerceptionPosition(const robot::perception::PerceptionPacket& packet) {
  if (!packet.has_position()) {
    return absl::InvalidArgumentError("PerceptionPacket has no position field");
  }
  return packet.position().position();
}

absl::Status RequirePerceptionImage(const robot::perception::PerceptionPacket& packet) {
  if (!packet.has_image()) {
    return absl::InvalidArgumentError("PerceptionPacket has no image field");
  }
  return absl::OkStatus();
}

absl::Status RequirePerceptionPointCloud(const robot::perception::PerceptionPacket& packet) {
  if (!packet.has_point_cloud()) {
    return absl::InvalidArgumentError("PerceptionPacket has no point_cloud field");
  }
  return absl::OkStatus();
}

}  // namespace ros2_utils
