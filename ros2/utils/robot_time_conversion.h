#pragma once

#include <cmath>
#include <cstdint>
#include <limits>

#include "absl/status/statusor.h"
#include "builtin_interfaces/msg/time.hpp"

namespace ros2_utils {

// Timestamp representation only: the caller must ensure both endpoints use
// the same time domain. No clock is read, synchronized, or changed here.
inline absl::StatusOr<builtin_interfaces::msg::Time> ToRosTimestamp(double timestamp) {
  if (!std::isfinite(timestamp))
    return absl::InvalidArgumentError("Timestamp seconds must be finite");
  const double whole = std::floor(timestamp);
  if (whole < std::numeric_limits<int32_t>::min() || whole > std::numeric_limits<int32_t>::max())
    return absl::OutOfRangeError("Timestamp exceeds ROS seconds range");
  int64_t sec = static_cast<int64_t>(whole);
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
  // Avoid cancellation for negative times with a small fractional magnitude.
  if (timestamp.sec < 0 && timestamp.nanosec != 0)
    return (static_cast<double>(timestamp.sec) + 1.0) -
           (1'000'000'000 - timestamp.nanosec) / 1'000'000'000.0;
  return static_cast<double>(timestamp.sec) + timestamp.nanosec / 1'000'000'000.0;
}

}  // namespace ros2_utils
