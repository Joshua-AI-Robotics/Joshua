#pragma once

#include <memory>
#include <string_view>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/robot_clock.pb.h"

namespace joshua {

// Fixed platform epoch: Unix epoch. Never use process startup as the epoch.
inline constexpr double kRobotStartTime = 0.0;
inline constexpr double kNanosecondsPerSecond = 1'000'000'000.0;

// Implementations must support concurrent reads. UTC/PTP return UTC seconds
// since kRobotStartTime; an explicitly selected MONOTONIC clock is boot-relative.
class RobotClock {
 public:
  virtual ~RobotClock() = default;
  virtual double Now() const = 0;
  virtual std::string_view SourceName() const {
    return "custom";
  }
};

// Pure validation: no clocks/devices are opened or read.
absl::Status ValidateRobotClockConfig(const config::RobotClockConfig& config);

// Probes the configured source once. PTP falls back to UTC unless require_ptp
// is set. Logs fallback reasons. Does not install the result globally.
absl::StatusOr<std::unique_ptr<RobotClock>> MakeRobotClock(const config::RobotClockConfig& config);

// Takes ownership for the process lifetime. Rejects null and any installation
// after an explicit or lazy initialization. No replacement/reset API exists.
absl::Status SetGlobalRobotClock(std::unique_ptr<RobotClock> clock);

// Shares one once_flag with SetGlobalRobotClock. Installs the default only if
// nothing has been installed. Throws std::runtime_error if creation fails;
// failed initialization can be retried by a subsequent caller.
void CheckGlobalClock();

// Safe from helpers, callbacks and worker threads. Runtime read failures throw
// std::runtime_error; they never return zero, stale data or switch clock sources.
// Use a monotonic duration clock for deadlines rather than UTC/PTP robot time.
double RobotTime();

}  // namespace joshua
