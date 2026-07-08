#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/actuator_interface.h"
#include "robot/board/interfaces/board_channel.h"

namespace robot::action {

// Actuator driver for MOTOR_TI_DEMO — a joint whose closed-loop
// controller runs on the board. The driver owns motor semantics only
// (limits, idle position, the degrees->native conversion); how a target
// reaches the controller is the channel's problem
// (docs/BOARD_LAYER_RFC.md §5.4). Replaces Am243EthercatDriver with
// byte-identical bus traffic on the AM243 TI demo path.
class JointDriver : public robot::action::ActuatorInterface {
 public:
  JointDriver(std::shared_ptr<robot::board::BoardChannel> channel,
              const robot::action::Actuator& action_config);
  ~JointDriver() override = default;

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
