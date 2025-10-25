#pragma once

#include <glog/logging.h>

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "robot/action/proto/action_packet.pb.h"

// Abstract action interface - high-level interface for all action components.
namespace robot::action {
class ActionInterface {
 public:
  ActionInterface() = default;
  virtual ~ActionInterface() = default;

  // Common interface methods for all action components
  virtual absl::Status Init() = 0;
  virtual std::string GetId() = 0;
  virtual absl::Status SetAction(const robot::action::ActionPacket& action_packet) = 0;
  virtual absl::Status Teardown() = 0;
};
}  // namespace robot::action
