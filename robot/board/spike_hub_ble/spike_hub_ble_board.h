#pragma once

#include <cstdint>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// BoardType::SPIKE_HUB_BLE — Pybricks hub over BLE (docs/BOARD_LAYER_RFC.md
// §5.6, §10 Phase 9). Stub only: pybricksdev (BLE scan/connect via Bleak,
// MicroPython program upload, line-protocol over BLE notify/write
// characteristics, retry/backoff) has no C++ equivalent library. A native
// port needs real BlueZ/GATT work plus the Pybricks hub-upload protocol,
// and a physical hub to validate against — genuine protocol work, not a
// board-layer wiring exercise like TeensyBoard's one-line constructor.
// TODO(hmoon): Implement the native BLE port. Until then, MOTOR_SPIKE
// stays on the Python fallback (robot/action/motors/drivers/
// pybricks_driver.py, robot/comm/pybricks_ble_transport.py); this stub
// only keeps BoardFactory's switch complete and fails fast with a message
// pointing at that path if anything points a board_name at a
// SPIKE_HUB_BLE board today.
class SpikeHubBleBoard : public BoardInterface {
 public:
  SpikeHubBleBoard() = default;

  absl::Status Init(const robot::board::Board& config) override {
    return absl::UnimplementedError(
        absl::StrCat("Board '",
                     config.name(),
                     "': SPIKE_HUB_BLE has no native implementation yet; use the Python "
                     "Pybricks path (docs/BOARD_LAYER_RFC.md §10 Phase 9)."));
  }

  absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) override {
    return absl::FailedPreconditionError("SpikeHubBleBoard was never initialized.");
  }

  absl::Status Teardown() override {
    return absl::OkStatus();
  }
};

}  // namespace robot::board
