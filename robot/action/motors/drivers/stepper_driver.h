#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/actuator_interface.h"
#include "robot/board/interfaces/board_channel.h"

namespace robot::action {

// Actuator driver for MOTOR_STEPPER_NEMA17: owns motor semantics (limits,
// idle position, degrees<->steps conversion) and hands the channel raw
// steps; the STEP/DIR pulse generation and any drive-level tunables
// (max pulse rate, direction inversion) live in the board
// (TeensyBoard/ArduinoBoard) so the same channel abstraction works no
// matter which MCU or transport is driving the TB6600 underneath
// (docs/BOARD_LAYER_RFC.md §5.4, §10 Phase 5). Board- and
// transport-agnostic: no comm headers, no board headers, no #ifdefs.
class StepperDriver : public robot::action::ActuatorInterface {
 public:
  StepperDriver(std::shared_ptr<robot::board::BoardChannel> channel,
                const robot::action::Actuator& action_config);
  ~StepperDriver() override = default;

  // ActionInterface methods.
  absl::Status Init() override;
  std::string GetId() override;
  absl::Status SetAction(const robot::action::ActionPacket& action_packet) override;
  absl::Status Teardown() override;

  // ActuatorInterface methods.
  absl::Status SetSpeed(float value) override;
  absl::Status SetPosition(float angle_deg) override;
  // TB6600's ENA pin is a binary holding-torque gate, not a continuous
  // target, so this gates Enable()/Disable() like Sts3215Driver's torque-
  // enable register (docs/BOARD_LAYER_RFC.md §12.7, resolved in
  // robot/board/interfaces/board_channel.h) rather than staging
  // TargetMode::kTorque.
  absl::Status SetTorque(float torque) override;
  absl::Status SetMiddlePosition() override;
  absl::Status SetIdlePosition() override;

 private:
  float DegreesToSteps(float angle_deg) const;

  std::shared_ptr<robot::board::BoardChannel> channel_;
  robot::action::Actuator action_config_;
  std::string id_;
  float steps_per_degree_ = 0.0f;
  float gear_ratio_ = 1.0f;
  float idle_position_ = 0.0f;
  float operational_lower_limit_ = 0.0f;
  float operational_upper_limit_ = 0.0f;
};

}  // namespace robot::action
