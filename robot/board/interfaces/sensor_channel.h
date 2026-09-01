#pragma once

#include <cstdint>

#include "absl/status/statusor.h"

namespace robot::board {

struct ChannelFeedback {
  float position = 0.0f;  // Channel's native unit (steps, ticks).
  float velocity = 0.0f;
  uint32_t fault_flags = 0;
};

// The measurement half of a board channel: read-only, and the only thing
// perception is ever handed (docs/BOARD_LAYER_RFC.md §5.3). A motor driver
// closing a position loop holds it too, via BoardChannel.
//
// This sample is deliberately scalar-shaped. A board is a device that
// multiplexes several channels over one shared link, and what fits in a
// register read or a PDO slot is a scalar. Rich payloads (an image, a point
// cloud) come from single-stream devices, which are not boards and reach
// perception through robot::comm::StreamTransport instead. Generalising
// this struct waits for a second board-attached sensor kind that needs it.
class SensorChannel {
 public:
  virtual ~SensorChannel() = default;
  virtual absl::StatusOr<ChannelFeedback> ReadFeedback() = 0;
};

}  // namespace robot::board
