#pragma once
#include "absl/status/status.h"
#include "robot/perception/proto/perception.pb.h"
namespace robot::perception {
inline absl::Status ValidateAudioConfig(const AlsaAudioConfig& config) {
  if (config.device().empty() || config.sample_rate_hz() < 8000 ||
      config.sample_rate_hz() > 192000 || config.channels() < 1 || config.channels() > 8 ||
      config.frames_per_chunk() < 1 || config.frames_per_chunk() > config.sample_rate_hz()) {
    return absl::InvalidArgumentError(
        "ALSA requires device, 8000..192000 Hz, 1..8 channels, and a chunk of 1 frame..1 second");
  }
  return absl::OkStatus();
}
}  // namespace robot::perception
