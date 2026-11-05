// Sensor observations retain device and host clock domains separately.
#pragma once
#include <chrono>
#include <cstdint>

#include "absl/status/statusor.h"

namespace robot::board {
struct ScalarObservation {
  float value = 0;
  uint32_t boot_id = 0, sequence = 0, dropped_acquisitions = 0;
  uint32_t session_id = 0, subscription_id = 0, period_us = 0;
  uint8_t function_id = 0, clock = 0, validity = 0;
  uint16_t unit = 0;
  uint64_t acquisition_us = 0, sequence_gaps = 0, overwritten = 0;
  std::chrono::steady_clock::time_point received_at;
  std::chrono::microseconds receipt_age{0};
  bool receipt_fresh = false, usable = false;
};
class ScalarSensor {
 public:
  virtual ~ScalarSensor() = default;
  virtual absl::StatusOr<ScalarObservation> Latest() const = 0;
  virtual absl::Status Close() = 0;
};
}  // namespace robot::board
