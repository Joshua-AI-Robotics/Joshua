#pragma once
#include "absl/status/status.h"
namespace robot::board {
// Hardware PWM, initially disabled. Duty is normalized [0,1]. Frequency comes
// from the configured resource. Teardown disables and releases the generator.
class PwmOutput {
 public:
  virtual ~PwmOutput() = default;
  virtual absl::Status SetDutyCycle(double duty) = 0;
  virtual absl::Status Enable() = 0;
  virtual absl::Status Disable() = 0;
  virtual absl::Status Close() = 0;  // Terminal release; retained handles cannot revive it.
};
}  // namespace robot::board
