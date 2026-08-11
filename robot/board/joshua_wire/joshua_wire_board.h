#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "firmware/common/joshua_wire_v1.h"
#include "robot/board/frame/frame_transport.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::board {

// Shared host-side implementation of the joshua_wire_v1 board contract
// (docs/BOARD_LAYER_RFC.md §7.2/§7.3/§7.5): open a FrameTransport, run the
// IDENTIFY handshake (board_id, protocol version, per-channel drive all
// cross-checked against config), push CONFIGURE_CHANNEL for every channel,
// then dispatch ENABLE/DISABLE/SET_TARGET/GET_FEEDBACK per channel. Every
// Joshua-firmware MCU board (TeensyBoard today; ArduinoBoard, a future
// ESP32 board, ...) speaks the exact same wire protocol, so this class
// holds that entire orchestration once — a concrete board subclasses this
// and supplies only the handful of facts that actually differ per board:
// which BoardType/jw1_board_id_t it is, and (if it ever isn't a plain
// serial link) how its comm config is validated and its production
// transport is built.
//
// What is NOT generic here, and stays a subclass's problem if it ever
// needs to differ: nothing today — every current and planned board on this
// class (Teensy, Arduino) is a serial FrameTransport speaking STEP_DIR
// channels, so ValidateComm/CreateProductionTransport below default to
// exactly that and no subclass has needed to override either yet.
class JoshuaWireBoard : public BoardInterface {
 public:
  ~JoshuaWireBoard() override = default;

  absl::Status Init(const robot::board::Board& config) final;
  absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) final;
  absl::Status Teardown() final;

  // Replaces the FrameTransport so the IDENTIFY/CONFIGURE_CHANNEL handshake
  // and channel dispatch are testable without real hardware
  // (docs/BOARD_LAYER_RFC.md §7.3). Pass nullptr to restore each
  // subclass's real CreateProductionTransport() path. Shared by every
  // subclass (one process-wide test seam, reset in TearDown) — safe as
  // long as tests run serially, which is gtest's default and already the
  // pattern this seam has always used.
  static void SetFrameTransportFactoryForTesting(
      std::function<absl::StatusOr<std::shared_ptr<FrameTransport>>(const robot::comm::Comm&)>
          factory);

 protected:
  // The BoardType this concrete board accepts; Init() rejects any other.
  virtual robot::board::BoardType ExpectedBoardType() const = 0;

  // The jw1_board_id_t IDENTIFY must report; Init() rejects a mismatch —
  // e.g. a re-enumerated serial path now pointing at a different board
  // (docs/BOARD_LAYER_RFC.md §7.5).
  virtual jw1_board_id_t ExpectedWireBoardId() const = 0;

  // Checked before any transport is opened. Default requires SERIAL — the
  // only comm type any joshua_wire_v1 board uses today; override if a
  // future variant (e.g. a UDP/W5500 firmware build) needs a different
  // one.
  virtual absl::Status ValidateComm(const robot::comm::Comm& comm,
                                    const std::string& board_name) const;

  // Builds the real (non-test) transport. Default opens
  // CommFactory::CreateSerial + SerialFrameTransport; override alongside
  // ValidateComm if a future variant's comm type differs.
  virtual absl::StatusOr<std::shared_ptr<FrameTransport>> CreateProductionTransport(
      const robot::comm::Comm& comm) const;

 private:
  // These two need ExpectedBoardType()/ExpectedWireBoardId(), so they're
  // methods (not the free functions in the .cc's anonymous namespace that
  // everything else is) — everything they check beyond that identity fact
  // is generic across every joshua_wire_v1 board.
  absl::Status ValidateConfig(const robot::board::Board& config) const;
  absl::Status IdentifyAndValidate(FrameTransport& transport,
                                   const robot::board::Board& config) const;

  bool initialized_ = false;
  robot::board::Board config_;
  std::shared_ptr<FrameTransport> transport_;
  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels_;
};

}  // namespace robot::board
