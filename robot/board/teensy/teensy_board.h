#pragma once

#include "firmware/common/joshua_wire_v1.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// BoardType::TEENSY41 — an MCU running Joshua-authored firmware, reached
// over a joshua_wire_v1 FrameTransport (docs/BOARD_LAYER_RFC.md §7.2/§7.3,
// §10 Phase 5). All protocol orchestration (IDENTIFY handshake, per-channel
// CONFIGURE_CHANNEL, ENABLE/DISABLE/SET_TARGET/GET_FEEDBACK dispatch) lives
// in JoshuaWireBoard, shared with every other Joshua-firmware MCU board
// (ArduinoBoard, a future ESP32 board, ...); this class supplies only the
// two facts that make a Teensy a Teensy — see JoshuaWireBoard's class
// comment (docs/BOARD_LAYER_RFC.md §7.3, revised) for what's generic vs.
// board-specific. Model-specific only in name: the wire protocol and
// JoshuaWireBoard are MCU-agnostic, and firmware/teensy/41/ is the one
// firmware image this board type currently pairs with.
class TeensyBoard : public JoshuaWireBoard {
 public:
  TeensyBoard() = default;

 protected:
  robot::board::BoardType ExpectedBoardType() const override;
  jw1_board_id_t ExpectedWireBoardId() const override;
};

}  // namespace robot::board
