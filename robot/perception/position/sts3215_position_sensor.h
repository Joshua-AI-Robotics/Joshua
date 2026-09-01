#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/interfaces/board_channel.h"
#include "robot/perception/interfaces/position_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace robot::perception {

// Position sensor for an STS3215 servo exposed by a Feetech bus board.
// Feetech framing and register access remain in FeetechBusBoard; this driver
// converts the selected servo channel's feedback into perception data.
class Sts3215PositionSensor : public PositionInterface {
 public:
  Sts3215PositionSensor(std::shared_ptr<robot::board::BoardChannel> channel,
                        const robot::perception::SinglePerception& config);
  ~Sts3215PositionSensor() override = default;

  absl::Status Init() override;
  std::string GetId() override;
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override;
  absl::Status Teardown() override;

 private:
  std::shared_ptr<robot::board::BoardChannel> channel_;
  std::string id_;
  robot::perception::PerceptionPacket reusable_packet_;
};

}  // namespace robot::perception
