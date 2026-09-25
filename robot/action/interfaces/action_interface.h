#pragma once

#include <glog/logging.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/action/proto/action_packet.pb.h"

// Abstract action interface - high-level interface for all action components.
namespace robot::action {
// Optional native controller feedback for bounded, open-loop stepper sessions.
struct ActionFeedback {
  double emitted_steps = 0;
  uint32_t fault_flags = 0;
};

class ActionInterface {
 public:
  ActionInterface() = default;
  virtual ~ActionInterface() = default;

  // Common interface methods for all action components
  virtual absl::Status Init() = 0;
  virtual std::string GetId() = 0;
  virtual absl::Status SetAction(const robot::action::ActionPacket& action_packet) = 0;
  virtual absl::Status Teardown() = 0;
  virtual absl::StatusOr<ActionFeedback> ReadFeedback() {
    return absl::UnimplementedError("Controller feedback is unavailable for this actuator");
  }
};
}  // namespace robot::action
