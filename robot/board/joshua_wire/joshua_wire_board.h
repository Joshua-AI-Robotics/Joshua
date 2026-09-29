#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "firmware/common/joshua_wire_commands.h"
#include "robot/board/frame/frame_transport.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/proto/board.pb.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::board {

class JoshuaWireCommandClient;

// Shared JoshuaWire engine: protocol reset, IDENTIFY, channel configuration and
// command dispatch. Identity is constructor data; transport capabilities come
// from CommFactory. V2 uses one session/ID allocator across message and optional
// cyclic endpoints. The session routes target/feedback to cyclic when present,
// otherwise all commands use messages. No serial/SOEM classes or NIC lifecycle
// live here. The v1 legacy message seam remains until its separate migration.
// ESP32 retains a post-open settle hook; moving that policy to serial config is
// still pending. STEP_DIR is the only implemented configuration payload today.
class JoshuaWireBoard : public BoardInterface {
 public:
  JoshuaWireBoard(robot::board::BoardType expected_board_type, jw_board_id_t expected_wire_board_id)
      : expected_board_type_(expected_board_type),
        expected_wire_board_id_(expected_wire_board_id) {}
  ~JoshuaWireBoard() override {
    Teardown().IgnoreError();
  }

  absl::Status Init(const robot::board::Board& config) final;
  absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) final;
  absl::Status Teardown() final;

 protected:
  // Checked before opening: a message-only or paired capability is required.
  // CommFactory validates the mechanism/config and produces ready endpoints.
  virtual absl::Status ValidateComm(const robot::comm::Comm& comm,
                                    const std::string& board_name) const;

  // Factory capabilities, not concrete mechanism implementations.
  virtual absl::StatusOr<robot::comm::CommTransport> CreateTransports(
      const robot::comm::Comm& comm) const;

 private:
  // These two need expected_board_type_/expected_wire_board_id_, so
  // they're methods (not the free functions in the .cc's anonymous
  // namespace that everything else is) — everything they check beyond
  // that identity fact is generic across every JoshuaWire board.
  absl::Status ValidateConfig(const robot::board::Board& config) const;
  absl::Status IdentifyAndValidate(JoshuaWireCommandClient& commands,
                                   const robot::board::Board& config) const;

  const robot::board::BoardType expected_board_type_;
  const jw_board_id_t expected_wire_board_id_;

  bool initialized_ = false;
  robot::board::Board config_;
  std::shared_ptr<JoshuaWireCommandClient> commands_;
  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels_;
};

}  // namespace robot::board
