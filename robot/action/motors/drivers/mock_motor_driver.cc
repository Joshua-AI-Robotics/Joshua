#include "robot/action/motors/drivers/mock_motor_driver.h"

#include <glog/logging.h>

#include <string>

namespace robot::action {

MockMotorDriver::MockMotorDriver(std::shared_ptr<robot::board::BoardChannel> channel,
                                 const robot::action::Actuator& action_config)
    : channel_(std::move(channel)), action_config_(action_config) {
  operational_lower_limit_ = action_config.operational_lower_limit();
  operational_upper_limit_ = action_config.operational_upper_limit();
  id_ = GetId();
  LOG(INFO) << "MockMotorDriver actuator ID: " << id_ << " initialized";
}

absl::Status MockMotorDriver::Init() {
  if (channel_ == nullptr) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Mock motor driver requires a board channel");
  }
  // Auto-enables, matching TiDemoDriver/StepperDriver: a mock actuator has
  // no safety case for withholding it.
  return channel_->Enable();
}

std::string MockMotorDriver::GetId() {
  if (!action_config_.actuator_name().empty()) {
    return "mock_motor_driver_" + action_config_.actuator_name();
  }
  return "mock_motor_driver_" + std::to_string(action_config_.id());
}

absl::Status MockMotorDriver::SetAction(const robot::action::ActionPacket& action_packet) {
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
          LOG(WARNING) << "Unknown mock preset command: " << action_packet.preset();
          return absl::OkStatus();
      }

    case robot::action::ActionPacket::kComplex: {
      const auto& complex_action = action_packet.complex();
      if (complex_action.has_speed()) {
        auto status = SetSpeed(complex_action.speed());
        if (!status.ok()) return status;
      }
      if (complex_action.has_torque()) {
        auto status = SetTorque(complex_action.torque());
        if (!status.ok()) return status;
      }
      if (complex_action.has_position()) {
        return SetPosition(complex_action.position());
      }
      if (complex_action.has_dc()) {
        LOG(WARNING) << "dc is not supported on mock motors, ignoring";
      }
      return absl::OkStatus();
    }

    case robot::action::ActionPacket::kPosition:
      return SetPosition(action_packet.position());
    case robot::action::ActionPacket::kTorque:
      return SetTorque(action_packet.torque());
    case robot::action::ActionPacket::kSpeed:
      return SetSpeed(action_packet.speed());
    case robot::action::ActionPacket::kDc:
      LOG(WARNING) << "dc is not supported on mock motors, ignoring";
      return absl::OkStatus();
    case robot::action::ActionPacket::ACTION_TYPE_NOT_SET:
    default:
      LOG(WARNING) << "No action type set in mock ActionPacket [ID: " << action_packet.action_id()
                   << "]";
      return absl::OkStatus();
  }
}

absl::Status MockMotorDriver::SetSpeed(float value) {
  return channel_->SetTarget(robot::board::TargetMode::kVelocity, value);
}

absl::Status MockMotorDriver::SetPosition(float value) {
  return channel_->SetTarget(robot::board::TargetMode::kPosition, value);
}

absl::Status MockMotorDriver::SetTorque(float torque) {
  if (torque < 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Mock motor torque must be non-negative");
  }
  if (torque > 0.0f) {
    return channel_->Enable();
  }
  return channel_->Disable();
}

absl::Status MockMotorDriver::SetMiddlePosition() {
  return SetPosition((operational_lower_limit_ + operational_upper_limit_) / 2.0f);
}

absl::Status MockMotorDriver::Teardown() {
  return SetTorque(0.0f);
}

}  // namespace robot::action
