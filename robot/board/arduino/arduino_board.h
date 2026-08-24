#pragma once

#include "firmware/common/joshua_wire_v1.h"
#include "robot/board/joshua_wire/joshua_wire_board.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// BoardType::ARDUINO_UNO — an MCU running Joshua-authored firmware, reached
// over a joshua_wire_v1 FrameTransport (docs/BOARD_LAYER_RFC.md §7.2/§7.3).
// Protocol orchestration lives in JoshuaWireBoard; this class supplies
// Uno identity plus the one Uno-specific bring-up fact: opening the CDC
// port pulses DTR, the 328P resets into the bootloader, and IDENTIFY
// misses that window unless production CreateTransport() waits out the
// reboot. Tests inject a FakeFrameTransport and skip that path. Paired
// firmware: firmware/arduino/uno/.
class ArduinoBoard : public JoshuaWireBoard {
 public:
  ArduinoBoard() : JoshuaWireBoard(robot::board::BoardType::ARDUINO_UNO, JW1_BOARD_ARDUINO_UNO) {}

 protected:
  absl::StatusOr<std::shared_ptr<FrameTransport>> CreateTransport(
      const robot::comm::Comm& comm) const override;
};

}  // namespace robot::board
