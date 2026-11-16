#pragma once

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/pin/pin_resource.h"

namespace robot::board {

// Digital I/O is independent of motor channels and JointCommand semantics.
// Handles stop working after board teardown, even if a consumer retains them.
class DigitalPin {
 public:
  virtual ~DigitalPin() = default;
  virtual const PinResource& resource() const = 0;
  virtual absl::StatusOr<bool> Read() = 0;
  virtual absl::Status Write(bool active) = 0;
};

}  // namespace robot::board
