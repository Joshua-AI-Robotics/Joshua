#include "robot/action/motors/drivers/sts3215_driver.h"

#include <glog/logging.h>

#include <chrono>
#include <string>
#include <thread>

#include "utils/status_macros.h"

namespace robot::action {

Sts3215Driver::Sts3215Driver(std::shared_ptr<robot::board::BoardChannel> channel,
                             const robot::action::Actuator& action_config)
    : channel_(std::move(channel)), action_config_(action_config) {
  operational_lower_limit_ = action_config.operational_lower_limit();
  operational_upper_limit_ = action_config.operational_upper_limit();
  idle_position_ = action_config.sts3215_config().idle_position();

  id_ = GetId();
  LOG(INFO) << "Sts3215Driver actuator ID: " << id_ << " initialized";
}

absl::Status Sts3215Driver::Init() {
  if (channel_ == nullptr) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "STS3215 driver requires a board channel");
  }
  // Best-effort seed of the channel's staged move speed with the actuator's
  // configured default, matching the pre-board-layer driver's constructor
  // (which initialized the same value locally and could not fail). Ignored
  // on error: a channel that rejects this pre-torque-enable write still
  // works correctly once the first explicit SetSpeed() call lands.
  channel_->SetTarget(robot::board::TargetMode::kVelocity, action_config_.sts3215_config().move_speed())
      .IgnoreError();
  return absl::OkStatus();
}

std::string Sts3215Driver::GetId() {
  if (!action_config_.actuator_name().empty()) {
    return "sts3215_driver_" + action_config_.actuator_name();
  }
  return "sts3215_driver_" + std::to_string(action_config_.id());
}

absl::Status Sts3215Driver::SetAction(const robot::action::ActionPacket& action_packet) {
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
        LOG(WARNING) << "dc is not supported on STS3215, ignoring";
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
      LOG(WARNING) << "dc is not supported on STS3215, ignoring";
      return absl::OkStatus();
    case robot::action::ActionPacket::ACTION_TYPE_NOT_SET:
    default:
      LOG(WARNING) << "No action type set in STS3215 ActionPacket [ID: " << action_packet.action_id()
                   << "]";
      return absl::OkStatus();
  }
}

absl::Status Sts3215Driver::SetSpeed(float value) {
  if (value < 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "STS3215 speed must be non-negative");
  }
  return channel_->SetTarget(robot::board::TargetMode::kVelocity, value);
}

absl::Status Sts3215Driver::SetPosition(float angle) {
  if (angle < operational_lower_limit_ || angle > operational_upper_limit_) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "STS3215 position is outside operational limits");
  }
  return channel_->SetTarget(robot::board::TargetMode::kPosition, angle);
}

// Torque semantics (docs/BOARD_LAYER_RFC.md §12.7, resolved for the board
// layer in robot/board/interfaces/board_channel.h): STS3215 only has a
// torque-*enable* register, so SetTorque gates Enable/Disable rather than
// staging a TargetMode::kTorque value.
absl::Status Sts3215Driver::SetTorque(float torque) {
  if (torque < 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "STS3215 torque must be non-negative");
  }
  if (torque > 0.0f) {
    return channel_->Enable();
  }
  return channel_->Disable();
}

absl::Status Sts3215Driver::SetMiddlePosition() {
  const float middle = (operational_lower_limit_ + operational_upper_limit_) / 2.0f;
  return channel_->SetTarget(robot::board::TargetMode::kPosition, middle);
}

absl::Status Sts3215Driver::SetIdlePosition() {
  // Bypasses the operational-limit check in SetPosition(): idle_position is
  // a calibration value that may legitimately sit just outside the
  // teleop-safe operational range (matches the pre-board-layer driver,
  // which wrote it unconditionally).
  return channel_->SetTarget(robot::board::TargetMode::kPosition, idle_position_);
}

absl::Status Sts3215Driver::Teardown() {
  // Slow down and give the servo time to reach idle before cutting torque,
  // so the arm settles instead of going slack mid-move (matches the
  // pre-board-layer driver's teardown sequence).
  ABSL_RETURN_IF_ERROR(SetSpeed(1000.0f));
  ABSL_RETURN_IF_ERROR(SetIdlePosition());
  std::this_thread::sleep_for(std::chrono::milliseconds(2000));
  return SetTorque(0.0f);
}

}  // namespace robot::action
