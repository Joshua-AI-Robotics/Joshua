#pragma once

#include "absl/status/status.h"
#include "robot/action/proto/action.pb.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// Central motor<->drive compatibility table (docs/BOARD_LAYER_RFC.md §5.5).
// ActionFactory calls this before constructing a motor driver, so an
// actuator bound to a channel whose drive cannot move that motor fails at
// Init() with an actionable error instead of mid-motion.
absl::Status ValidateMotorChannel(robot::action::MotorType motor_type,
                                  robot::board::DriveInterface drive);

}  // namespace robot::board
