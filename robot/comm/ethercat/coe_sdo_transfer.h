// Incremental CoE SDO transfer for the JW-sized mailbox objects. This is a
// transport codec/state machine, not a board protocol or test utility. Each
// Step performs at most one bounded register operation; it never retries writes.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::comm::ethercat {

class CoeSdoTransfer {
 public:
  using Bytes = std::vector<uint8_t>;
  struct Mailbox {
    uint16_t write_offset;
    uint16_t read_offset;
    uint16_t write_size;
    uint16_t read_size;
  };
  // One configured-station datagram, WKC must be exactly one. The implementation
  // must use nonblocking send and a single total receive deadline, no retries.
  class RegisterIo {
   public:
    virtual ~RegisterIo() = default;
    virtual absl::Status Read(uint16_t offset, Bytes& bytes, int budget_us) = 0;
    virtual absl::Status Write(uint16_t offset, const Bytes& bytes, int budget_us) = 0;
  };

  // Reads use capacity; writes use bytes. Supports expedited and unsegmented
  // normal transfers up to 76 bytes, covering the plan's largest JW envelope.
  // Counter is the master's next transmit mailbox counter for this slave, in
  // 1..7. The slave's transmit counter is independent. No I/O in Begin.
  absl::Status Begin(Mailbox mailbox,
                     uint16_t index,
                     uint8_t subindex,
                     uint8_t counter,
                     bool write,
                     Bytes bytes,
                     size_t capacity);
  // nullopt means pending, Bytes means completed (empty for write), status means
  // failed. Retained mailboxes are drained before sending; malformed replies
  // fail closed. Mailbox counters are not request/response correlation IDs.
  absl::StatusOr<std::optional<Bytes>> Step(RegisterIo& io, int budget_us);
  void Cancel();

 private:
  enum class Phase { kIdle, kDrainStatus, kDrainRead, kWriteStatus, kWrite, kReadStatus, kRead };
  absl::StatusOr<std::optional<Bytes>> Decode(const Bytes& response);
  Phase phase_ = Phase::kIdle;
  Mailbox mailbox_{};
  uint16_t index_ = 0;
  uint8_t subindex_ = 0;
  bool write_ = false;
  size_t capacity_ = 0;
  Bytes request_;
};

}  // namespace robot::comm::ethercat
