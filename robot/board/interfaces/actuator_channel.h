#pragma once

#include "absl/status/status.h"

namespace robot::board {

enum class TargetMode { kPosition, kVelocity, kTorque };

// The command half of a board channel. Motor drivers hold this; perception
// never does, so a sensor cannot command motion even by mistake
// (docs/BOARD_LAYER_RFC.md §5.3).
//
// Values are in the board's native unit; the motor driver owns the
// degrees<->native conversion.
class ActuatorChannel {
 public:
  virtual ~ActuatorChannel() = default;
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
};

}  // namespace robot::board
