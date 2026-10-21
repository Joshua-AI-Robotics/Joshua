#pragma once

#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// UART binding for the current shared JoshuaWire engine. USB-bridge reset
// settling is configured through SerialConfig.post_open_settle_ms.
class Esp32Board : public JoshuaWireBoard {
 public:
  Esp32Board() : JoshuaWireBoard(robot::board::BoardType::ESP32) {}
};

}  // namespace robot::board
