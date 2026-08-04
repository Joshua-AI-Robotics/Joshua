#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::board {

// Byte-level transport boundary for joshua_wire_v1 frame exchange
// (docs/BOARD_LAYER_RFC.md §7.2/§7.3): frame-based Joshua-firmware boards
// (TeensyBoard today, ArduinoBoard later) depend on this, not a concrete
// transport, so frame dispatch is testable without real hardware. The
// frame itself is transport-agnostic; SendAndReceive is atomic
// (Write-then-Read) so another channel's request cannot interleave on a
// shared half-duplex link — mirrors robot::comm::SerialTransport::AtomicRead.
//
// Every joshua_wire_v1 response has a fixed size for a given proto_ver
// (see JW1_*_RESPONSE_PAYLOAD_LEN in firmware/common/joshua_wire_v1.h), so
// callers always know expected_response_len up front — no incremental
// header-then-body read is needed at this boundary.
class FrameTransport {
 public:
  virtual ~FrameTransport() = default;
  virtual absl::StatusOr<std::vector<uint8_t>> SendAndReceive(
      const std::vector<uint8_t>& request_frame, size_t expected_response_len) = 0;
};

}  // namespace robot::board
