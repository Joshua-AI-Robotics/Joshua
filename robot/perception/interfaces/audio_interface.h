#pragma once
#include <chrono>
#include <cstdint>

#include "robot/perception/interfaces/perception_interface.h"
namespace robot::perception {
struct AudioCaptureSchedule {
  std::chrono::nanoseconds poll_interval{};
  uint64_t max_packets_per_poll = 0;
};

class AudioInterface : public PerceptionInterface {
 public:
  ~AudioInterface() override = default;
  // Valid after successful Init. Reopening may change the negotiated schedule;
  // consumers must refresh their timer after reads, including pending reads.
  virtual AudioCaptureSchedule GetCaptureSchedule() = 0;
};
}  // namespace robot::perception
