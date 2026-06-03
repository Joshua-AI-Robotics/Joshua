#pragma once

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/statusor.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace ros2_utils {

float MapNormalizedPosition(float value, float lower, float upper);
float DenormalizePositionValue(float value, float lower, float upper);

void DenormalizeActionPacket(robot::action::ActionPacket& packet, float lower, float upper);

absl::StatusOr<robot::action::ActionPacket> ParseActionPacket(const void* data, int size);
absl::StatusOr<robot::action::ActionPacket> ParseActionPacket(const std::vector<uint8_t>& data);
std::string SerializeActionPacket(const robot::action::ActionPacket& packet);

absl::StatusOr<robot::perception::PerceptionPacket> ParsePerceptionPacket(const void* data,
                                                                          int size);
absl::StatusOr<robot::perception::PerceptionPacket> ParsePerceptionPacket(
    const std::vector<uint8_t>& data);
std::string SerializePerceptionPacket(const robot::perception::PerceptionPacket& packet);

absl::StatusOr<float> ExtractPositionFromAction(const robot::action::ActionPacket& packet);
std::optional<float> ExtractScalarFromAction(const robot::action::ActionPacket& packet);

absl::StatusOr<robot::perception::PerceptionPacket> ActionToPerceptionPositionPacket(
    const robot::action::ActionPacket& action,
    std::optional<std::pair<float, float>> limits = std::nullopt);

absl::StatusOr<float> RequirePerceptionPosition(const robot::perception::PerceptionPacket& packet);
absl::Status RequirePerceptionImage(const robot::perception::PerceptionPacket& packet);
absl::Status RequirePerceptionPointCloud(const robot::perception::PerceptionPacket& packet);

std::optional<std::string> PerceptionDataKind(const robot::perception::PerceptionPacket& packet);

}  // namespace ros2_utils
