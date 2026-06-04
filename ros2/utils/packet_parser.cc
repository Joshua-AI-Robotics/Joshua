#include "ros2/utils/packet_parser.h"

#include <algorithm>
#include <cctype>

#include "absl/strings/str_join.h"

namespace ros2_utils {
namespace {

using robot::action::ActionPacket;

template <typename Fn>
void ApplyToPositionSources(ActionPacket& packet, Fn transform) {
  if (packet.has_position()) {
    packet.set_position(transform(packet.position()));
  }
  if (packet.has_complex() && packet.complex().has_position()) {
    packet.mutable_complex()->set_position(transform(packet.complex().position()));
  }
}

// Allowed /<device_id>/<action_type> topic suffixes for Float32 actuator commands.
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
