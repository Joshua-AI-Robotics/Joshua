#include "robot/action/motors/drivers/stepper_driver.h"

#include <glog/logging.h>

#include <cmath>
#include <limits>
#include <string>

namespace robot::action {

StepperDriver::StepperDriver(std::shared_ptr<robot::board::BoardChannel> channel,
                             const robot::action::Actuator& action_config)
    : channel_(std::move(channel)), action_config_(action_config) {
  operational_lower_limit_ = action_config.operational_lower_limit();
  operational_upper_limit_ = action_config.operational_upper_limit();
  steps_per_degree_ = action_config.stepper_config().steps_per_degree();
  gear_ratio_ = action_config.stepper_config().gear_ratio();
  idle_position_ = action_config.stepper_config().idle_position();

  id_ = GetId();
  LOG(INFO) << "StepperDriver actuator ID: " << id_ << " initialized";
}

float StepperDriver::DegreesToSteps(float angle_deg) const {
  return angle_deg * steps_per_degree_ * gear_ratio_;
}

absl::Status StepperDriver::Init() {
  if (channel_ == nullptr) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Stepper driver requires a board channel");
  }
  // Auto-enables (unlike Sts3215Driver): a TB6600's
  // ENA pin is a binary holding-torque gate with no real safety case for
  // withholding it on an open-loop stepper the way STS3215's torque-enable
  // register has for a precision servo. Found via real hardware testing —
  // a demo preset driving position over a single Float32 topic has no path
  // to send an explicit enable command, so requiring one silently no-ops
  // every SetPosition call.
  return channel_->Enable();
}

std::string StepperDriver::GetId() {
  if (!action_config_.actuator_name().empty()) {
    return "stepper_driver_" + action_config_.actuator_name();
  }
  return "stepper_driver_" + std::to_string(action_config_.id());
}

absl::Status StepperDriver::SetAction(const robot::action::ActionPacket& action_packet) {
  switch (action_packet.action_type_case()) {
    case robot::action::ActionPacket::kPreset:
      switch (action_packet.preset()) {
        case robot::action::PresetCommand::PRESET_MIDDLE_POSITION:
          return SetMiddlePosition();
        case robot::action::PresetCommand::PRESET_IDLE_POSITION:
          return SetIdlePosition();
        case robot::action::PresetCommand::PRESET_TEARDOWN:
          return Teardown();
        case robot::action::PresetCommand::PRESET_ENABLE_TORQUE:
          return SetTorque(1.0f);
        case robot::action::PresetCommand::PRESET_DISABLE_TORQUE:
          return SetTorque(0.0f);
        default:
          LOG(WARNING) << "Unknown preset command: " << action_packet.preset();
          return absl::OkStatus();
      }

    case robot::action::ActionPacket::kJoint: {
      auto command = action_packet.joint();
      auto validation = ValidateJointCommand(command, action_config_.actuator_name());
      if (!validation.ok()) return validation;
      // Validate every field before issuing any channel writes.
      // TODO(hmoon): Support SI velocity commands once BoardChannel and joshua_wire
      // preserve combined command fields and firmware honors requested speed.
      // Define position/velocity semantics explicitly; effort requires separate
      // hardware capability support and must not be treated as a torque-enable gate.
      if (command.units() == JointCommand::SI && (command.has_velocity() || command.has_effort()))
        return absl::UnimplementedError("Stepper driver has no SI velocity/effort contract");
      if (command.position_encoding() == JointCommand::POSITION_SI) {
        if (command.has_position()) {
          command.set_position(command.position() * (180.0 / 3.14159265358979323846));
          command.set_position_encoding(JointCommand::POSITION_NATIVE);
        }
      } else if (command.position_encoding() != JointCommand::POSITION_NATIVE) {
        return absl::InvalidArgumentError(
            "Driver requires resolved native or SI position encoding");
      }
      if (command.has_effort())
        return absl::UnimplementedError(
            "Stepper driver has no effort control; use presets for torque enable/disable");
      for (double value : {command.position(), command.velocity(), command.effort()}) {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max() ||
            (value != 0 && static_cast<float>(value) == 0))
          return absl::OutOfRangeError("Stepper joint value exceeds native float range");
      }
      if (command.has_position() && (command.position() < operational_lower_limit_ ||
                                     command.position() > operational_upper_limit_))
        return absl::InvalidArgumentError("Stepper position is outside operational limits");
      if ((command.has_velocity() && command.velocity() < 0) ||
          (command.has_effort() && command.effort() < 0))
        return absl::InvalidArgumentError("Stepper requires nonnegative native speed and effort");
      // TODO(hmoon): Convert supported joint fields into one structured board
      // command. On joshua_wire, SetSpeed followed by SetPosition overwrites the
      // velocity target; it does not set a move-speed limit. Define position +
      // velocity semantics, gate on board capabilities, and preserve all supplied
      // fields. Enable SI velocity only after the backend honors its magnitude.
      if (command.has_velocity()) {
        auto status = SetSpeed(static_cast<float>(command.velocity()));
        if (!status.ok()) return status;
      }
      if (command.has_effort()) {
        auto status = SetTorque(static_cast<float>(command.effort()));
        if (!status.ok()) return status;
      }
      if (command.has_position()) return SetPosition(static_cast<float>(command.position()));
      return absl::OkStatus();
    }
    case robot::action::ActionPacket::ACTION_TYPE_NOT_SET:
    default:
      LOG(WARNING) << "No action type set in stepper ActionPacket [ID: "
                   << action_packet.action_id() << "]";
      return absl::InvalidArgumentError("ActionPacket requires joint or preset");
  }
}

absl::Status StepperDriver::SetSpeed(float value) {
  if (value < 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Stepper speed must be non-negative");
  }
  return channel_->SetTarget(robot::board::TargetMode::kVelocity, DegreesToSteps(value));
}

absl::Status StepperDriver::SetPosition(float angle_deg) {
  if (angle_deg < operational_lower_limit_ || angle_deg > operational_upper_limit_) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Stepper position is outside operational limits");
  }
  return channel_->SetTarget(robot::board::TargetMode::kPosition, DegreesToSteps(angle_deg));
}

absl::Status StepperDriver::SetTorque(float torque) {
  if (torque < 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Stepper torque must be non-negative");
  }
  if (torque > 0.0f) {
    return channel_->Enable();
  }
  return channel_->Disable();
}

absl::Status StepperDriver::SetMiddlePosition() {
  const float middle = (operational_lower_limit_ + operational_upper_limit_) / 2.0f;
  return SetPosition(middle);
}

absl::Status StepperDriver::SetIdlePosition() {
  // Bypasses the operational-limit check in SetPosition(): idle_position is
  // a calibration value that may legitimately sit outside the teleop-safe
  // operational range (matches Sts3215Driver::SetIdlePosition).
  return channel_->SetTarget(robot::board::TargetMode::kPosition, DegreesToSteps(idle_position_));
}

absl::Status StepperDriver::Teardown() {
  auto idle_status = SetIdlePosition();
  if (!idle_status.ok()) {
    return idle_status;
  }
  return SetTorque(0.0f);
}

}  // namespace robot::action
