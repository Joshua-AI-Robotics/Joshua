#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "utils/robot_time.h"

namespace robot::perception {

// Expose the process-global clock to sensors without per-sensor clock state.
using ::joshua::RobotTime;

// Common lifecycle and sampling interface for all sensors.
class PerceptionInterface {
 public:
  PerceptionInterface() = default;
  virtual ~PerceptionInterface() = default;

  virtual absl::Status Init() = 0;
  virtual std::string GetId() = 0;
  virtual absl::StatusOr<robot::perception::PerceptionPacket> GetData() = 0;
  // Nonblocking, idempotent terminal stop request; may run concurrently with Init/GetData.
  // Never take a mutex held across device I/O here. The default is for finite reads
  // (e.g. board transactions): the owner stops scheduling and waits for their bound.
  // Uninterruptible backends must document process escalation rather than promise
  // a bounded join. Teardown runs only after acquisition has returned.
  virtual void RequestStop() noexcept {}
  virtual absl::Status Teardown() = 0;
};

}  // namespace robot::perception
