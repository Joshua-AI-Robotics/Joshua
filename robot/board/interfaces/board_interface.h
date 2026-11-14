#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/interfaces/board_channel.h"
#include "robot/board/interfaces/digital_pin.h"
#include "robot/board/interfaces/scalar_sensor.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// Shared interface to an addressable hardware controller.
class BoardInterface {
 public:
  virtual ~BoardInterface() = default;
  // Acquires/configures resources. External controllers also open comm and
  // perform their protocol handshake; local GPIO does not use a transport.
  virtual absl::Status Init(const robot::board::Board& config) = 0;
  virtual absl::StatusOr<std::shared_ptr<BoardChannel>> OpenChannel(uint32_t index) = 0;
  virtual absl::StatusOr<std::shared_ptr<DigitalPin>> OpenDigitalPin(const std::string& name) {
    return absl::UnimplementedError("Board does not support digital pins");
  }
  virtual absl::StatusOr<std::shared_ptr<ScalarSensor>> SubscribeScalar(uint32_t function,
                                                                        uint32_t period_us) {
    return absl::UnimplementedError("Board does not support scalar subscriptions");
  }
  virtual absl::Status Teardown() = 0;
  virtual absl::Status EmergencyStop() {
    return absl::UnimplementedError("Board has no emergency stop capability");
  }
};

}  // namespace robot::board
