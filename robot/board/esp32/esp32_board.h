#pragma once

#include "firmware/common/joshua_wire_v1.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// BoardType::ESP32 — an MCU running Joshua-authored firmware, reached over
// a joshua_wire_v1 FrameTransport, same family as TeensyBoard
// (docs/BOARD_LAYER_RFC.md §7.2/§7.3, §10 Phase 5). All protocol
// orchestration lives in JoshuaWireBoard; this class supplies only the two
// facts that make an ESP32 an ESP32, as constructor data — see
// robot/board/teensy/teensy_board.h for the identical pattern, and
// JoshuaWireBoard's class comment for why identity is a constructor
// argument, not a virtual hook. firmware/esp32/ is the one firmware image
// this board type currently pairs with — a plain UART/USB-serial STEP_DIR
// image, the same shape as Teensy, not the Wi-Fi/UDP transport variant
// docs/BOARD_LAYER_RFC.md speculated ESP32 might eventually prove (that
// would need a robot::comm::UdpTransport and a UdpFrameTransport, neither
// of which exist yet — see frame_transport.h's TODO).
class Esp32Board : public JoshuaWireBoard {
 public:
  Esp32Board() : JoshuaWireBoard(robot::board::BoardType::ESP32, JW1_BOARD_ESP32) {}
};

}  // namespace robot::board
