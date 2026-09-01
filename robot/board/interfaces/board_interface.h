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
  // Read-only handle on the same slot, for perception. The default narrows
  // the full channel, which is right for every board whose channels both
  // drive and sense (a Feetech servo always does). A board with
  // sensor-only channels — a bare quadrature input or ADC pin on an MCU,
  // which has no actuator half to open — overrides this.
  virtual absl::StatusOr<std::shared_ptr<SensorChannel>> OpenSensorChannel(uint32_t index) {
    auto channel = OpenChannel(index);
    if (!channel.ok()) {
      return channel.status();
    }
    return std::shared_ptr<SensorChannel>(*channel);
  }
  virtual absl::Status Teardown() = 0;
};

}  // namespace robot::board
