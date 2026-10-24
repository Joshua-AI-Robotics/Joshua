#pragma once

#include <time.h>

#include <memory>
#include <string>

#include "utils/robot_time.h"

namespace joshua::internal {

// System-call boundary for hardware-free source-selection and failure tests.
// Implementations must support concurrent reads; clocks retain its ownership.
class ClockIo {
 public:
  virtual ~ClockIo() = default;
  virtual absl::StatusOr<int> OpenPtp(const std::string& path) = 0;
  virtual absl::StatusOr<timespec> Read(clockid_t id) const = 0;
  virtual void Close(int fd) = 0;
};

clockid_t PtpClockId(int fd);
absl::StatusOr<std::unique_ptr<RobotClock>> MakeRobotClockWithIo(
    const config::RobotClockConfig& config, std::shared_ptr<ClockIo> io);

}  // namespace joshua::internal
