// Correlated request/response capability backed by a cyclic transport. This is
// a contract, not an EtherCAT implementation; no PDO/SOEM types escape here.
#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace robot::comm {

// Exchanges cyclic control messages, such as target updates and feedback carried
// by an EtherCAT PDO worker. Each Exchange returns the response matching its request.
// JoshuaWire adapters support only:
//   SET_TARGET   - update a channel's position, velocity or torque target.
//   GET_FEEDBACK - read a channel's position, velocity and fault flags.
// Management commands (RESET_SESSION, IDENTIFY, CONFIGURE_CHANNEL, ENABLE,
// DISABLE and ESTOP) use MessageTransport. The board engine validates routing.
//
// CommFactory supplies a ready-to-use endpoint. A worker owns cyclic I/O; callers
// wait without driving the bus. One request may be in flight per endpoint.
// Implementations must validate the response correlation tuple, reject stale
// replies, quarantine timed-out IDs and wake blocked calls on stop/link loss.
class CorrelatedCyclicTransport {
 public:
  virtual ~CorrelatedCyclicTransport() = default;

  // A finite, positive timeout bounds the entire call, including slot waiting.
  // Borrow request bytes until return; copy them into adapter-owned queue/slot
  // storage. Returned response bytes are owned by the caller. A post-publication
  // timeout has unknown execution outcome and must not trigger an implicit retry.
  virtual absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request,
                                                        absl::Duration timeout) = 0;
};
}  // namespace robot::comm
