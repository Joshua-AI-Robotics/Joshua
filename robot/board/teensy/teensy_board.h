#pragma once

#include "firmware/common/joshua_wire_commands.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// Teensy 4.1 identity for the shared JoshuaWire engine.
class TeensyBoard : public JoshuaWireBoard {
 public:
  TeensyBoard() : JoshuaWireBoard(robot::board::BoardType::TEENSY41, JW_BOARD_TEENSY41) {}
};

}  // namespace robot::board
