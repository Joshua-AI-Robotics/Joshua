#pragma once

#include "robot/perception/interfaces/audio_interface.h"

namespace robot::perception {
// Bound each poll by one capture buffer plus a partially assembled packet.
// The driver returns DeadlineExceeded only when currently available data cannot
// complete a packet. Even an always-ready plugin cannot monopolize the caller.
template <typename Consumer>
absl::Status DrainAudio(AudioInterface& source, Consumer consume) {
  const auto budget = source.GetCaptureSchedule().max_packets_per_poll;
  for (uint64_t i = 0; i < budget; ++i) {
    auto packet = source.GetData();
    if (!packet.ok()) return packet.status();
    consume(*packet);
  }
  return absl::OkStatus();
}
}  // namespace robot::perception
