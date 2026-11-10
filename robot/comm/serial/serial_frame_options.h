#pragma once
#include <climits>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "absl/status/status.h"

namespace robot::comm {
struct SerialFrameOptions {
  uint32_t frame_idle_timeout_ms = 100;
  size_t max_reply_frames = 4;
  size_t max_notification_frames = 32;
  auto Values() const {
    return std::tie(frame_idle_timeout_ms, max_reply_frames, max_notification_frames);
  }
  bool operator==(const SerialFrameOptions& other) const {
    return Values() == other.Values();
  }
  bool operator!=(const SerialFrameOptions& other) const {
    return !(*this == other);
  }
};
inline absl::Status ValidateSerialFrameOptions(const SerialFrameOptions& p) {
  if (p.frame_idle_timeout_ms == 0 || p.frame_idle_timeout_ms > INT_MAX ||
      p.max_reply_frames == 0 || p.max_reply_frames > 256 || p.max_notification_frames == 0 ||
      p.max_notification_frames > 256)
    return absl::InvalidArgumentError("Invalid serial frame policy");
  return absl::OkStatus();
}
}  // namespace robot::comm
