#pragma once
#include "absl/status/statusor.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "sensor_msgs/msg/image.hpp"
namespace ros2_utils {
// Validates a tightly packed RGB8/BGR8 packet before indexing; timestamp is set by publisher.
absl::StatusOr<sensor_msgs::msg::Image> CameraPacketImage(
    const robot::perception::PerceptionPacket& packet);
}  // namespace ros2_utils
