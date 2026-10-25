#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "absl/status/statusor.h"
#include "builtin_interfaces/msg/time.hpp"
#include "utils/robot_time.h"

namespace ros2_utils {

// Convert UTC/PTP RobotTime seconds to/from Unix-epoch ROS headers.
// This translates the epoch only; it does not synchronize clock domains.
inline absl::StatusOr<builtin_interfaces::msg::Time> ToRosTimestamp(double timestamp) {
  if (!std::isfinite(timestamp))
    return absl::InvalidArgumentError("Timestamp seconds must be finite");
  const double whole = std::floor(timestamp);
  const double ros_whole = whole + joshua::kRobotStartTime;
  if (ros_whole < std::numeric_limits<int32_t>::min() ||
      ros_whole > std::numeric_limits<int32_t>::max())
    return absl::OutOfRangeError("Timestamp exceeds ROS seconds range");
  int64_t sec = static_cast<int64_t>(ros_whole);
  int64_t nanosec = std::llround((timestamp - whole) * 1'000'000'000);
  if (nanosec == 1'000'000'000) {
    ++sec;
    nanosec = 0;
  }
  if (sec > std::numeric_limits<int32_t>::max())
    return absl::OutOfRangeError("Rounded timestamp exceeds ROS seconds range");
  builtin_interfaces::msg::Time result;
  result.sec = static_cast<int32_t>(sec);
  result.nanosec = static_cast<uint32_t>(nanosec);
  return result;
}

inline absl::StatusOr<double> FromRosTimestamp(const builtin_interfaces::msg::Time& timestamp) {
  if (timestamp.nanosec >= 1'000'000'000)
    return absl::InvalidArgumentError("ROS timestamp nanosec must be less than 1e9");
  const double seconds = static_cast<double>(timestamp.sec) - joshua::kRobotStartTime;
  // Avoid cancellation for negative times with a small fractional magnitude.
  if (seconds < 0 && timestamp.nanosec != 0)
    return (seconds + 1.0) -
           (1'000'000'000 - timestamp.nanosec) / 1'000'000'000.0;
  return seconds + timestamp.nanosec / 1'000'000'000.0;
}

}  // namespace ros2_utils
