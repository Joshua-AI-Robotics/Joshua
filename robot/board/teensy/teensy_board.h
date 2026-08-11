#pragma once

#include <functional>
#include <map>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/frame/frame_transport.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::board {

// BoardType::TEENSY41 — an MCU running Joshua-authored firmware, reached
// over a joshua_wire_v1 FrameTransport (docs/BOARD_LAYER_RFC.md §7.2/§7.3,
// §10 Phase 5). Init() opens the transport, runs the IDENTIFY handshake
// (board_id, protocol version, and per-channel drive all cross-checked
// against config so a wrong device on this port, or a config/firmware
// drift, fails here, not mid-motion — §7.5), then pushes CONFIGURE_CHANNEL
// for every STEP_DIR channel. Model-specific only in name: the wire
// protocol and this class are MCU-agnostic, and firmware/teensy/41/ is the
// one firmware image this board type currently pairs with.
class TeensyBoard : public BoardInterface {
 public:
  TeensyBoard() = default;

  absl::Status Init(const robot::board::Board& config) override;
  absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) override;
  absl::Status Teardown() override;

  // Replaces the FrameTransport so the IDENTIFY/CONFIGURE_CHANNEL handshake
  // and channel dispatch are testable without a real Teensy
  // (docs/BOARD_LAYER_RFC.md §7.3). Pass nullptr to restore the default
  // CommFactory::CreateSerial + SerialFrameTransport path. For tests.
  static void SetFrameTransportFactoryForTesting(
      std::function<absl::StatusOr<std::shared_ptr<FrameTransport>>(const robot::comm::Comm&)>
          factory);

 private:
  bool initialized_ = false;
  robot::board::Board config_;
  std::shared_ptr<FrameTransport> transport_;
  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels_;
};

}  // namespace robot::board
