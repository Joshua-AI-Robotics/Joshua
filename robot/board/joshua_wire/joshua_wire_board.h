#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "firmware/common/joshua_wire_commands.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

class JoshuaWireCommandClient;

// Shared JoshuaWire engine: protocol reset, IDENTIFY, channel configuration and
// command dispatch. CommFactory supplies message-only or paired message/cyclic
// capabilities. JW shares one session across both planes. Link lifecycle and timing belong to comm.
// Only STEP_DIR channel configuration is implemented.
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

 private:
  absl::Status ValidateConfig(const robot::board::Board& config) const;
  absl::Status IdentifyAndValidate(JoshuaWireCommandClient& commands,
                                   const robot::board::Board& config) const;

  const robot::board::BoardType expected_board_type_;
  const jw_board_id_t expected_wire_board_id_;

  bool initialized_ = false;
  std::string name_;
  std::shared_ptr<JoshuaWireCommandClient> commands_;
  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels_;
};

}  // namespace robot::board
