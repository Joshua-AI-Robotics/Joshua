// Complete-frame duplex capability. Exactly one protocol coordinator consumes it.
#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace robot::comm {
class FrameStreamTransport {
 public:
  using Clock = std::chrono::steady_clock;
  struct ReceivedFrame {
    std::vector<uint8_t> bytes;
    Clock::time_point received_at;
  };
  struct WriteResult {
    absl::Status status;
    // Accepted by the OS, not confirmed delivery/execution. Nonzero means the
    // command may have reached the device, including on timeout or disconnect.
    size_t bytes_written = 0;
  };
  struct Diagnostics {
    size_t assembling_bytes = 0;
    size_t replies_pending = 0;
    size_t notifications_pending = 0;
    uint64_t received_frames = 0;
    uint64_t malformed_frames = 0;
    uint64_t partial_frames_expired = 0;
    uint64_t notifications_dropped = 0;
    uint64_t reply_overflows = 0;
  };
  virtual ~FrameStreamTransport() = default;
  // Atomic frame submission. Never interrupt a partial frame to publish another:
  // a partial-write failure faults the link. The absolute deadline includes wait.
  virtual WriteResult WriteFrame(absl::Span<const uint8_t> frame, Clock::time_point deadline) = 0;
  // Replies have reserved storage and precede telemetry. Whole notifications may
  // be coalesced/dropped with counters; a reply overflow faults instead of losing
  // ownership silently. Receipt times are retained through queues/coalescing.
  virtual absl::StatusOr<ReceivedFrame> Receive(Clock::time_point deadline) = 0;
  // Interrupt a current/next receive wait for scheduling/cancellation, without
  // closing the link or discarding frames. At most one pending wake is retained.
  virtual void Interrupt() = 0;
  virtual absl::Status status() const = 0;
  virtual absl::Status Close() = 0;  // Terminal, idempotent; wakes waiters.
  virtual Diagnostics SnapshotFrames() const = 0;
};
}  // namespace robot::comm
