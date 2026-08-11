#include "robot/board/teensy/teensy_board.h"

namespace robot::board {

robot::board::BoardType TeensyBoard::ExpectedBoardType() const {
  return robot::board::BoardType::TEENSY41;
}

jw1_board_id_t TeensyBoard::ExpectedWireBoardId() const {
  return JW1_BOARD_TEENSY41;
}

}  // namespace robot::board
