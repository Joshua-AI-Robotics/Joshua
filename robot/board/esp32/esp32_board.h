#pragma once

#include "firmware/common/joshua_wire_v1.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// BoardType::ESP32 — an MCU running Joshua-authored firmware, reached over
// a joshua_wire_v1 FrameTransport, same family as TeensyBoard
// (docs/BOARD_LAYER_RFC.md §7.2/§7.3, §10 Phase 5). Identity (BoardType,
// jw1_board_id_t) is constructor data, same pattern as
// robot/board/teensy/teensy_board.h — see JoshuaWireBoard's class comment
// for why. One real difference from Teensy: CreateTransport() is
// overridden (see the .cc) to add a post-open settle delay, because most
// ESP32 dev boards reset when the serial port is opened, unlike Teensy's
// native-USB CDC. firmware/esp32/ is the one firmware image this board
// type currently pairs with — a plain UART/USB-serial STEP_DIR image, the
// same shape as Teensy, not the Wi-Fi/UDP transport variant
// docs/BOARD_LAYER_RFC.md speculated ESP32 might eventually prove (that
// would need a robot::comm::UdpTransport and a UdpFrameTransport, neither
// of which exist yet — see frame_transport.h's TODO).
class Esp32Board : public JoshuaWireBoard {
 public:
  Esp32Board() : JoshuaWireBoard(robot::board::BoardType::ESP32, JW1_BOARD_ESP32) {}

 protected:
  // Overrides the default only to add a post-open settle delay — see the
  // .cc for why: opening the port resets the board on most ESP32 dev
  // boards, unlike Teensy's native-USB CDC.
  absl::StatusOr<std::shared_ptr<FrameTransport>> CreateTransport(
      const robot::comm::Comm& comm) const override;
};

}  // namespace robot::board
