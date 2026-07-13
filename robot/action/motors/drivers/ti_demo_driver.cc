#include "robot/action/motors/drivers/ti_demo_driver.h"

#include <glog/logging.h>

#include <string>

namespace robot::action {

namespace {

// Full-scale of the channel's native position/torque range in counts,
// matching the AM243 TI demo seed byte (0-255). For firmware-owned joints
// the native unit is a firmware fact, so the generic driver that replaces
// this one should obtain it from the channel/board contract (e.g. channel
// metadata or ACTUATOR_V1 unit config), not a hardcoded constant.
constexpr float kNativeFullScale = 255.0f;

}  // namespace

TiDemoDriver::TiDemoDriver(std::shared_ptr<robot::board::BoardChannel> channel,
                           const robot::action::Actuator& action_config)
    : channel_(std::move(channel)), action_config_(action_config) {
  operational_lower_limit_ = action_config.operational_lower_limit();
  operational_upper_limit_ = action_config.operational_upper_limit();

  // Bridge until Phase 4 moves idle_position to a motor-level config
  // (docs/BOARD_LAYER_RFC.md §10): the deprecated AM243 config is the only
  // place a joint idle position lives today.
  if (action_config.has_am243_ethercat_config()) {
    idle_position_ = action_config.am243_ethercat_config().idle_position();
  }

  id_ = GetId();
  LOG(INFO) << "TiDemoDriver actuator ID: " << action_config_.id() << " initialized";
}

absl::Status TiDemoDriver::Init() {
  if (channel_ == nullptr) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "TI demo driver requires a board channel");
  }
  return channel_->Enable();
}

std::string TiDemoDriver::GetId() {
  if (!action_config_.actuator_name().empty()) {
    return "ti_demo_driver_" + action_config_.actuator_name();
  }
  return "ti_demo_driver_" + std::to_string(action_config_.id());
}

absl::Status TiDemoDriver::SetAction(const robot::action::ActionPacket& action_packet) {
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
          LOG(WARNING) << "Unknown joint preset command: " << action_packet.preset();
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
        LOG(WARNING) << "dc is not supported on joint actuator, ignoring";
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
      LOG(WARNING) << "dc is not supported on joint actuator, ignoring";
      return absl::OkStatus();
    case robot::action::ActionPacket::ACTION_TYPE_NOT_SET:
    default:
      LOG(WARNING) << "No action type set in joint ActionPacket [ID: " << action_packet.action_id()
                   << "]";
      return absl::OkStatus();
  }
}

absl::Status TiDemoDriver::SetSpeed(float value) {
  if (value < 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Joint speed must be non-negative");
  }
  return channel_->SetTarget(robot::board::TargetMode::kVelocity, value);
}

absl::Status TiDemoDriver::SetPosition(float angle) {
  if (angle < operational_lower_limit_ || angle > operational_upper_limit_) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Joint position is outside operational limits");
  }
  const float range = operational_upper_limit_ - operational_lower_limit_;
  if (range <= 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "Joint operational position range is invalid");
  }
  const float normalized = (angle - operational_lower_limit_) / range;
  return channel_->SetTarget(robot::board::TargetMode::kPosition, normalized * kNativeFullScale);
}

absl::Status TiDemoDriver::SetTorque(float torque) {
  if (torque < 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "Joint torque must be non-negative");
  }
  return channel_->SetTarget(robot::board::TargetMode::kTorque, torque * kNativeFullScale);
}

absl::Status TiDemoDriver::SetMiddlePosition() {
  return SetPosition((operational_lower_limit_ + operational_upper_limit_) / 2.0f);
}

absl::Status TiDemoDriver::SetIdlePosition() {
  return SetPosition(idle_position_);
}

absl::Status TiDemoDriver::Teardown() {
  auto idle_status = SetIdlePosition();
  if (!idle_status.ok()) {
    return idle_status;
  }

  auto torque_status = SetTorque(0.0f);
  if (!torque_status.ok()) {
    return torque_status;
  }

  return absl::OkStatus();
}

}  // namespace robot::action
