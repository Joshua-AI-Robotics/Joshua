#pragma once

#include "absl/status/status.h"
#include "robot/board/proto/board.pb.h"
#include "robot/perception/proto/perception.pb.h"

namespace robot::board {

// Which sensor meanings a signal leg can produce — the sensing counterpart
// of ValidateMotorChannel (docs/BOARD_LAYER_RFC.md §5.5). PerceptionFactory
// calls this before opening a channel, so a sensor bound to a channel that
// cannot produce its reading fails at Init() with an actionable error
// instead of publishing something meaningless.
//
// This is a capability question about one axis value, not a sensor x device
// table: what a SERVO_BUS_REGISTER read yields is a property of the signal
// leg, and stays true no matter which board or comm carries it.
absl::Status ValidateSensorChannel(robot::perception::SensorType sensor_type,
                                   robot::board::SignalInterface signal);

}  // namespace robot::board
