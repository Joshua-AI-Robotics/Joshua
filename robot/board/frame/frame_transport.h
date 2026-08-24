#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::board {

// Byte-level transport boundary for joshua_wire_v1 frame exchange
// (docs/BOARD_LAYER_RFC.md §7.2/§7.3): frame-based Joshua-firmware boards
// (TeensyBoard, ArduinoBoard) depend on this, not a concrete
// transport, so frame dispatch is testable without real hardware. The
// frame itself is transport-agnostic; SendAndReceive is atomic
// (Write-then-Read) so another channel's request cannot interleave on a
// shared half-duplex link — mirrors robot::comm::SerialTransport::AtomicRead.
//
// Every joshua_wire_v1 response has a fixed size for a given proto_ver
// (see JW1_*_RESPONSE_PAYLOAD_LEN in firmware/common/joshua_wire_v1.h), so
// callers always know expected_response_len up front — no incremental
// header-then-body read is needed at this boundary.
//
// Not tied to any particular transport's reliability characteristics:
// SendAndReceive returning a non-ok Status already models "no response
// arrived in time" as an expected, first-class outcome (see
// SerialFrameTransport's timeout handling) — not a guarantee every send
// gets an answer. This is what makes the interface transport-agnostic:
// TODO(docs/BOARD_LAYER_RFC.md §7.3/§10 Phase 5) a future
// UdpFrameTransport (unimplemented — no UDP-based board exists yet) would
// satisfy this exact contract over CommFactory::CreateUdp (also
// unimplemented) instead of CreateSerial, with zero changes needed here
// or in JoshuaWireBoard's IDENTIFY/CONFIGURE_CHANNEL/channel-dispatch
// logic — only JoshuaWireBoard::CreateTransport() would need a
// per-board override.
class FrameTransport {
 public:
  virtual ~FrameTransport() = default;
  virtual absl::StatusOr<std::vector<uint8_t>> SendAndReceive(
      const std::vector<uint8_t>& request_frame, size_t expected_response_len) = 0;
};

}  // namespace robot::board
