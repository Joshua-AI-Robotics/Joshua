#pragma once

#include "firmware/common/joshua_wire_commands.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// ESP32 identity for the shared JoshuaWire engine. Link reset/settle behavior
// belongs to SerialConfig, not this board: USB bridges may require an explicit
// post_open_settle_ms even when the MCU/protocol is otherwise identical.
class Esp32Board : public JoshuaWireBoard {
 public:
  Esp32Board() : JoshuaWireBoard(robot::board::BoardType::ESP32, JW_BOARD_ESP32) {}
};

}  // namespace robot::board
