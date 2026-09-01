#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/interfaces/board_channel.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// One controller shared by everything that names it: two actuators, or an
// actuator and a sensor, on the same board share one instance and one comm
// handle, and therefore one bus mutex (docs/BOARD_LAYER_RFC.md §5.3).
// Instances come from BoardFactory.
class BoardInterface {
 public:
  virtual ~BoardInterface() = default;
  // Opens comm, runs the IDENTIFY handshake, pushes CONFIGURE_CHANNEL setup.
  virtual absl::Status Init(const robot::board::Board& config) = 0;
  virtual absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) = 0;
  virtual absl::Status Teardown() = 0;
};

}  // namespace robot::board
