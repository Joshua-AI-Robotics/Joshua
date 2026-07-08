#pragma once

#include <map>
#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/interfaces/board_interface.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// Transport handle, resolved PDO region, and the bus mutex, shared between
// the board and every open channel so a channel outliving the board stays
// safe. Defined in am243_board.cc.
struct Am243SharedState;

// BoardType::AM243 — an AM243 slave reached over EtherCAT PDOs. Init owns
// the full SOEM bring-up (ConfigureSlaves -> StartCyclic -> verify
// OPERATIONAL), enforces the split LRD/LWR process-data mode the TI demo
// firmware requires, and resolves the slave's PDO region; channels stage
// targets into that region and ship them through the shared master
// transport (docs/BOARD_LAYER_RFC.md §5.7). The master is cached per NIC by
// CommFactory, so two boards daisy-chained on one interface share one
// transport and one socket.
class Am243Board : public BoardInterface {
 public:
  Am243Board() = default;

  absl::Status Init(const robot::board::Board& config) override;
  absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) override;
  absl::Status Teardown() override;

 private:
  bool initialized_ = false;
  robot::board::Board config_;
  std::shared_ptr<Am243SharedState> state_;
  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels_;
};

}  // namespace robot::board
