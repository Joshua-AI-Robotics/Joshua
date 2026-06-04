#pragma once

#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace ros2_utils {

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
