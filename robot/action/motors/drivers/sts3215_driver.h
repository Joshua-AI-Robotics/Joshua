#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/actuator_interface.h"
#include "robot/board/interfaces/board_channel.h"

namespace robot::action {

// Actuator driver for MOTOR_STS3215: owns motor semantics (limits, idle
// position, torque gating) and hands the channel raw ticks; the Feetech
// register-protocol encoding and bus serialization live in the board
// (FeetechBusBoard / feetech_protocol.h) so bus traffic is shared correctly
// with any other channel on the same port (docs/BOARD_LAYER_RFC.md §5.6,
// §10 Phase 4). Replaces the pre-board-layer Sts3215Driver, which owned the
// serial port and the register protocol directly.
class Sts3215Driver : public robot::action::ActuatorInterface {
 public:
  Sts3215Driver(std::shared_ptr<robot::board::BoardChannel> channel,
                const robot::action::Actuator& action_config);
  ~Sts3215Driver() override = default;

  // ActionInterface methods.
  absl::Status Init() override;
  std::string GetId() override;
  absl::Status SetAction(const robot::action::ActionPacket& action_packet) override;
  absl::Status Teardown() override;

  // ActuatorInterface methods.
  absl::Status SetSpeed(float value) override;
  absl::Status SetPosition(float angle) override;
  absl::Status SetTorque(float torque) override;
  absl::Status SetMiddlePosition() override;
  absl::Status SetIdlePosition() override;

 private:
  std::shared_ptr<robot::board::BoardChannel> channel_;
  robot::action::Actuator action_config_;
  std::string id_;
  float operational_lower_limit_ = 0.0f;
  float operational_upper_limit_ = 0.0f;
  float idle_position_ = 0.0f;
};

}  // namespace robot::action
