#pragma once

#include "absl/status/status.h"
#include "robot/board/proto/board.pb.h"
#include "robot/perception/proto/perception.pb.h"

namespace robot::board {

// Central sensor<->drive compatibility table, the perception-side twin of
// ValidateMotorChannel (docs/BOARD_LAYER_RFC.md §5.5). PerceptionFactory
// calls this before opening a channel, so an encoder bound to a channel
// that cannot report its position fails at Init() with an actionable error
// instead of publishing garbage.
absl::Status ValidateSensorChannel(robot::perception::EncoderType encoder_type,
                                   robot::board::DriveInterface drive);

}  // namespace robot::board
