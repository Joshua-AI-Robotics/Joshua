#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/interfaces/board_channel.h"
#include "robot/perception/interfaces/encoder_interface.h"
#include "robot/perception/proto/perception.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"

namespace robot::perception {

// Reads joint position from a BoardChannel. Nothing here is specific to a
// servo family: the register protocol, the port and the bus mutex all live
// in the board (FeetechBusBoard / feetech_protocol.h), exactly as they do
// for Sts3215Driver on the action side (docs/BOARD_LAYER_RFC.md §5.6, §10
// Phase 4). Replaces the pre-board-layer Sts3215Encoder, which owned a
// serial port and a second, independent copy of the Feetech read codec.
//
// Values are the channel's native unit (ticks); the operational limits in
// config are carried for consumers but not applied here, matching the
// previous encoder's behaviour.
class BoardEncoder : public EncoderInterface {
 public:
  BoardEncoder(std::shared_ptr<robot::board::BoardChannel> channel,
               const robot::perception::Encoder& encoder_config);
  ~BoardEncoder() override = default;

  absl::Status Init() override;
  std::string GetId() override;
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override;
  absl::StatusOr<float> GetPosition() override;
  absl::Status Teardown() override;

 private:
  std::shared_ptr<robot::board::BoardChannel> channel_;
  std::string id_;
  robot::perception::PerceptionPacket reusable_packet_;
};

}  // namespace robot::perception
