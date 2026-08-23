#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/actuator_interface.h"
#include "robot/board/interfaces/board_channel.h"

namespace robot::action {

// Actuator driver for MOTOR_MOCK: a board-layer stand-in for pipeline
// validation and tests, replacing the Python MockDriver
// (docs/BOARD_LAYER_RFC.md §10 Phase 9). Deliberately permissive — no unit
// conversion, no operational-limit checks on SetPosition/SetSpeed — since
// its job is to prove the config -> ActionFactory -> BoardFactory ->
// MockBoard resolution flow works, not to model a real motor. Torque
// follows the resolved Enable/Disable convention (docs/BOARD_LAYER_RFC.md
// §12.7), same as StepperDriver/TiDemoDriver. SetIdlePosition is not
// overridden — MockMotorConfig has no idle-position field, so it falls
// through to ActuatorInterface's default (log + OkStatus).
class MockMotorDriver : public robot::action::ActuatorInterface {
 public:
  MockMotorDriver(std::shared_ptr<robot::board::BoardChannel> channel,
                  const robot::action::Actuator& action_config);
  ~MockMotorDriver() override = default;

  // ActionInterface methods.
  absl::Status Init() override;
  std::string GetId() override;
  absl::Status SetAction(const robot::action::ActionPacket& action_packet) override;
  absl::Status Teardown() override;

  // ActuatorInterface methods.
  absl::Status SetSpeed(float value) override;
  absl::Status SetPosition(float value) override;
  absl::Status SetTorque(float torque) override;
  absl::Status SetMiddlePosition() override;

 private:
  std::shared_ptr<robot::board::BoardChannel> channel_;
  robot::action::Actuator action_config_;
  std::string id_;
  float operational_lower_limit_ = 0.0f;
  float operational_upper_limit_ = 0.0f;
};

}  // namespace robot::action
