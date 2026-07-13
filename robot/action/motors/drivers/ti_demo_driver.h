#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/actuator_interface.h"
#include "robot/board/interfaces/board_channel.h"

namespace robot::action {

// Actuator driver for MOTOR_TI_DEMO, the AM243 TI demo placeholder: it
// owns joint semantics (limits, idle position, degrees->native
// conversion) and hands the channel a demo seed value; how the target
// reaches the board is the channel's problem (docs/BOARD_LAYER_RFC.md
// §5.4). Replaces Am243EthercatDriver with byte-identical bus traffic.
// Deliberately narrow — retires with MOTOR_TI_DEMO. The eventual generic
// firmware-owned-joint driver should read its native units from the
// channel/board contract instead of this file's hardcoded full scale.
class TiDemoDriver : public robot::action::ActuatorInterface {
 public:
  TiDemoDriver(std::shared_ptr<robot::board::BoardChannel> channel,
               const robot::action::Actuator& action_config);
  ~TiDemoDriver() override = default;

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
