#pragma once

#include <cstddef>
#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "robot/comm/interfaces/message_framer.h"

namespace robot::board {

// Delimits joshua_wire_v1 frames from their sync and length bytes; CRC and
// payload validation remain the codec's job.
class JoshuaWireV1Framer : public robot::comm::MessageFramer {
 public:
  absl::StatusOr<size_t> RemainingBytes(absl::Span<const uint8_t> received) const override;
};

}  // namespace robot::board
