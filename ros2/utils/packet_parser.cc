#include "ros2/utils/packet_parser.h"

#include <algorithm>

#include "absl/status/status.h"

namespace ros2_utils {
namespace {

using robot::action::ActionPacket;
using robot::perception::PerceptionPacket;

template <typename Fn>
void ApplyToPositionSources(ActionPacket& packet, Fn transform) {
  if (packet.has_position()) {
    packet.set_position(transform(packet.position()));
  }
  if (packet.has_complex() && packet.complex().has_position()) {
    packet.mutable_complex()->set_position(transform(packet.complex().position()));
  }
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

absl::StatusOr<ActionPacket> ParseActionPacket(const void* data, const int size) {
  ActionPacket packet;
  if (!packet.ParseFromArray(data, size)) {
    return absl::InvalidArgumentError("Failed to parse ActionPacket");
  }
  return packet;
}

absl::StatusOr<ActionPacket> ParseActionPacket(const std::vector<uint8_t>& data) {
  return ParseActionPacket(data.data(), static_cast<int>(data.size()));
}

std::string SerializeActionPacket(const ActionPacket& packet) {
  return packet.SerializeAsString();
}

absl::StatusOr<PerceptionPacket> ParsePerceptionPacket(const void* data, const int size) {
  PerceptionPacket packet;
  if (!packet.ParseFromArray(data, size)) {
    return absl::InvalidArgumentError("Failed to parse PerceptionPacket");
  }
  return packet;
}

absl::StatusOr<PerceptionPacket> ParsePerceptionPacket(const std::vector<uint8_t>& data) {
  return ParsePerceptionPacket(data.data(), static_cast<int>(data.size()));
}

std::string SerializePerceptionPacket(const PerceptionPacket& packet) {
  return packet.SerializeAsString();
}

absl::StatusOr<float> ExtractPositionFromAction(const ActionPacket& packet) {
  if (packet.has_position()) {
    return packet.position();
  }
  if (packet.has_complex() && packet.complex().has_position()) {
    return packet.complex().position();
  }
  return absl::InvalidArgumentError(
      "ActionPacket has no position field (expected position or complex.position)");
}

std::optional<float> ExtractScalarFromAction(const ActionPacket& packet) {
  switch (packet.action_type_case()) {
    case ActionPacket::kPosition:
      return packet.position();
    case ActionPacket::kSpeed:
      return packet.speed();
    case ActionPacket::kTorque:
      return packet.torque();
    case ActionPacket::kDc:
      return packet.dc();
    default:
      return std::nullopt;
  }
}

absl::StatusOr<PerceptionPacket> ActionToPerceptionPositionPacket(
    const ActionPacket& action, const std::optional<std::pair<float, float>> limits) {
  auto position = ExtractPositionFromAction(action);
  if (!position.ok()) {
    return position.status();
  }

  float resolved = position.value();
  if (action.normalized()) {
    if (!limits.has_value()) {
      return absl::InvalidArgumentError(
          "normalized ActionPacket requires limits for PerceptionPacket conversion");
    }
    resolved = DenormalizePositionValue(resolved, limits->first, limits->second);
  }

  PerceptionPacket perception;
  perception.mutable_position()->set_position(resolved);
  if (action.timestamp_ns() != 0) {
    perception.set_timestamp_ns(action.timestamp_ns());
  }
  return perception;
}

absl::StatusOr<float> RequirePerceptionPosition(const PerceptionPacket& packet) {
  if (!packet.has_position()) {
    return absl::InvalidArgumentError("PerceptionPacket has no position field");
  }
  return packet.position().position();
}

absl::Status RequirePerceptionImage(const PerceptionPacket& packet) {
  if (!packet.has_image()) {
    return absl::InvalidArgumentError("PerceptionPacket has no image field");
  }
  return absl::OkStatus();
}

absl::Status RequirePerceptionPointCloud(const PerceptionPacket& packet) {
  if (!packet.has_point_cloud()) {
    return absl::InvalidArgumentError("PerceptionPacket has no point_cloud field");
  }
  return absl::OkStatus();
}

std::optional<std::string> PerceptionDataKind(const PerceptionPacket& packet) {
  switch (packet.data_type_case()) {
    case PerceptionPacket::kImage:
      return "image";
    case PerceptionPacket::kPosition:
      return "position";
    case PerceptionPacket::kSensor:
      return "sensor";
    case PerceptionPacket::kPointCloud:
      return "point_cloud";
    default:
      return std::nullopt;
  }
}

}  // namespace ros2_utils
