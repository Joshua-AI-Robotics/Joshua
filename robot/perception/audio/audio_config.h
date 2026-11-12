#pragma once
#include <chrono>

#include "absl/status/status.h"
#include "robot/perception/proto/perception.pb.h"
namespace robot::perception {
// Preserve the original buffer request for presets written before this field.
inline constexpr auto kDefaultCaptureBufferDuration = std::chrono::milliseconds(200);
// Keep capture buffering within the maximum supported packet duration.
inline constexpr auto kMaxCaptureBufferDuration = std::chrono::seconds(1);
inline std::chrono::milliseconds CaptureBufferDuration(const AlsaAudioConfig& config) {
  return config.has_capture_buffer_duration_ms()
             ? std::chrono::milliseconds(config.capture_buffer_duration_ms())
             : kDefaultCaptureBufferDuration;
}
inline absl::Status ValidateAudioConfig(const AlsaAudioConfig& config) {
  if (config.device().empty() || config.sample_rate_hz() < 8000 ||
      config.sample_rate_hz() > 192000 || config.channels() < 1 || config.channels() > 8 ||
      config.frames_per_chunk() < 1 || config.frames_per_chunk() > config.sample_rate_hz()) {
    return absl::InvalidArgumentError(
        "ALSA requires device, 8000..192000 Hz, 1..8 channels, and a chunk of 1 frame..1 second");
  }
  const auto buffer_duration = CaptureBufferDuration(config);
  if (buffer_duration <= std::chrono::milliseconds::zero() ||
      buffer_duration > kMaxCaptureBufferDuration) {
    return absl::InvalidArgumentError("ALSA capture_buffer_duration_ms must be 1..1000");
  }
  return absl::OkStatus();
}
}  // namespace robot::perception
