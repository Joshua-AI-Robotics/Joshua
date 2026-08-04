#pragma once

#include <cstdint>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::board {

enum class TargetMode { kPosition, kVelocity, kTorque };

struct ChannelFeedback {
  float position = 0.0f;  // Channel's native unit (steps, ticks).
  float velocity = 0.0f;
  uint32_t fault_flags = 0;
};

// One motor slot on one board. How the command reaches the controller
// (serial frame, UDP frame, EtherCAT PDO field, servo bus register write)
// is the board's problem. Values are in the board's native unit; the motor
// driver owns the degrees<->native conversion (docs/BOARD_LAYER_RFC.md §5.3).
class BoardChannel {
 public:
  virtual ~BoardChannel() = default;
  // Torque semantics (docs/BOARD_LAYER_RFC.md §12.7, decided in Phase 4):
  // Enable/Disable is the on/off gate — use it for any board whose torque
  // is fundamentally a binary enable register (STS3215's torque-enable
  // byte). Reserve TargetMode::kTorque for boards with a genuine continuous
  // torque target; a channel with no such register returns
  // UnimplementedError from it rather than reinterpreting it as on/off.
  virtual absl::Status Enable() = 0;
  virtual absl::Status Disable() = 0;
  // A board that cannot do a mode returns UnimplementedError from it.
  virtual absl::Status SetTarget(TargetMode mode, float value) = 0;
  virtual absl::StatusOr<ChannelFeedback> ReadFeedback() = 0;
};

}  // namespace robot::board
