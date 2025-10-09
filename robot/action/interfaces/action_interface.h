#pragma once

#include <vector>
#include <string>
#include <glog/logging.h>
#include <memory>
#include "robot/action/proto/action_packet.pb.h"
#include "absl/status/status.h"

// Abstract action interface - high-level interface for all action components.
namespace robot::action{
class ActionInterface{
  public:
    ActionInterface() = default;
    virtual ~ActionInterface() = default;
    
    // Common interface methods for all action components
    virtual absl::StatusOr<std::string> GetId() = 0;
    virtual absl::Status SetAction(const robot::action::ActionPacket& action_packet) = 0;
};
}