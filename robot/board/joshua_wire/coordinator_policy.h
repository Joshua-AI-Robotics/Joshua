// Hardware-free host policy resolution; no transport/factory/device dependency.
#pragma once
#include <climits>
#include <cstddef>
#include <cstdint>
#include <tuple>

#include "absl/status/status.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {
struct CoordinatorPolicy {
  uint32_t operation_timeout_ms = 200;
  uint32_t attempt_timeout_ms = 50;
  uint32_t polling_timeout_ms = 40;
  uint32_t max_attempts = 2;
  size_t max_pending_operations = 16;
  size_t max_pending_bytes = 4096;
  uint32_t max_priority_burst = 8;
  uint32_t scheduling_guard_ms = 1;
  size_t max_notifications = 32;
  size_t max_streams = 16;
  uint32_t telemetry_bytes_per_second = 0;
  auto Values() const {
    return std::tie(operation_timeout_ms,
                    attempt_timeout_ms,
                    polling_timeout_ms,
                    max_attempts,
                    max_pending_operations,
                    max_pending_bytes,
                    max_priority_burst,
                    scheduling_guard_ms,
                    max_notifications,
                    max_streams,
                    telemetry_bytes_per_second);
  }
  bool operator==(const CoordinatorPolicy& p) const {
    return Values() == p.Values();
  }
  bool operator!=(const CoordinatorPolicy& p) const {
    return !(*this == p);
  }
};
inline CoordinatorPolicy ResolveCoordinatorPolicy(const JoshuaWireHostPolicy& p) {
  CoordinatorPolicy o;
#define JW_POLICY_FIELD(field) \
  if (p.has_##field()) o.field = p.field()
  JW_POLICY_FIELD(operation_timeout_ms);
  JW_POLICY_FIELD(attempt_timeout_ms);
  JW_POLICY_FIELD(polling_timeout_ms);
  JW_POLICY_FIELD(max_attempts);
  JW_POLICY_FIELD(max_pending_operations);
  JW_POLICY_FIELD(max_pending_bytes);
  JW_POLICY_FIELD(max_priority_burst);
  JW_POLICY_FIELD(scheduling_guard_ms);
  JW_POLICY_FIELD(max_notifications);
  JW_POLICY_FIELD(max_streams);
#undef JW_POLICY_FIELD
  o.telemetry_bytes_per_second = p.telemetry_bytes_per_second();
  return o;
}
inline absl::Status ValidateCoordinatorPolicy(const CoordinatorPolicy& p) {
  for (auto t : {p.operation_timeout_ms,
                 p.attempt_timeout_ms,
                 p.polling_timeout_ms,
                 p.scheduling_guard_ms}) {
    if (t == 0 || t > INT_MAX) return absl::InvalidArgumentError("Invalid coordinator time budget");
  }
  if (p.attempt_timeout_ms > p.operation_timeout_ms ||
      p.polling_timeout_ms > p.operation_timeout_ms || p.max_attempts == 0 || p.max_attempts > 4 ||
      p.max_pending_operations == 0 || p.max_pending_operations > 1024 ||
      p.max_pending_bytes < 192 || p.max_pending_bytes > 1048576 || p.max_priority_burst == 0 ||
      p.max_priority_burst > 1024 || p.max_notifications == 0 || p.max_notifications > 256 ||
      p.max_streams == 0 || p.max_streams > 255)
    return absl::InvalidArgumentError("Invalid coordinator queue/retry/stream limits");
  return absl::OkStatus();
}
}  // namespace robot::board
