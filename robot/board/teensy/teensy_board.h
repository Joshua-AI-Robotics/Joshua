#pragma once

#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// Native USB binding for the current shared JoshuaWire engine.
class TeensyBoard : public JoshuaWireBoard {
 public:
  TeensyBoard() : JoshuaWireBoard(robot::board::BoardType::TEENSY41) {}
};

}  // namespace robot::board
