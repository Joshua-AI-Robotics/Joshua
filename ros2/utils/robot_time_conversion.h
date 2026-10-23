#pragma once

#include <cstdint>
#include <limits>

#include "absl/status/statusor.h"
#include "builtin_interfaces/msg/time.hpp"

namespace ros2_utils {

// Timestamp representation only: the caller must ensure both endpoints use
// the same time domain. No clock is read, synchronized, or changed here.
inline absl::StatusOr<builtin_interfaces::msg::Time> ToRosTimestamp(int64_t timestamp_ns) {
  constexpr int64_t kBillion = 1'000'000'000;
  int64_t sec = timestamp_ns / kBillion;
  int64_t nanosec = timestamp_ns % kBillion;
  if (nanosec < 0) {
    --sec;
    nanosec += kBillion;
  }
  if (sec < std::numeric_limits<int32_t>::min() || sec > std::numeric_limits<int32_t>::max())
    return absl::OutOfRangeError("Timestamp exceeds ROS seconds range");
  builtin_interfaces::msg::Time result;
  result.sec = static_cast<int32_t>(sec);
  result.nanosec = static_cast<uint32_t>(nanosec);
  return result;
}

inline absl::StatusOr<int64_t> FromRosTimestamp(const builtin_interfaces::msg::Time& timestamp) {
  if (timestamp.nanosec >= 1'000'000'000)
    return absl::InvalidArgumentError("ROS timestamp nanosec must be less than 1e9");
  return static_cast<int64_t>(timestamp.sec) * 1'000'000'000 + timestamp.nanosec;
}

}  // namespace ros2_utils
