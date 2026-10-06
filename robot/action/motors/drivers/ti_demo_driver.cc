#include "robot/action/motors/drivers/ti_demo_driver.h"

#include <glog/logging.h>

#include <cmath>
#include <limits>
#include <string>

namespace robot::action {

namespace {

// Full-scale of the channel's native position/torque range in counts,
// matching the AM243 TI demo seed byte (0-255). For firmware-owned joints
// the native unit is a firmware fact, so the generic driver that replaces
// this one should obtain it from the channel/board contract (e.g. channel
// metadata or ACTUATOR_V1 unit config), not a hardcoded constant.
// TODO(piscesgh): Move this scale into the channel/board contract when
// the ACTUATOR_V1 generic joint driver replaces TiDemoDriver.
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

    case robot::action::ActionPacket::kJoint: {
      auto command = action_packet.joint();
      auto validation = ValidateJointCommand(command, action_config_.actuator_name());
      if (!validation.ok()) return validation;
      // Validate every field before issuing any channel writes.
      if (command.units() == JointCommand::SI && (command.has_velocity() || command.has_effort()))
        return absl::UnimplementedError("TI demo driver has no SI velocity/effort contract");
      if (command.position_encoding() == JointCommand::POSITION_SI) {
        if (command.has_position()) {
          return absl::UnimplementedError("TI demo driver has no SI position contract");
        }
      } else if (command.position_encoding() != JointCommand::POSITION_NATIVE) {
        return absl::InvalidArgumentError(
            "Driver requires resolved native or SI position encoding");
      }
      for (double value : {command.position(), command.velocity(), command.effort()}) {
        if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max() ||
            (value != 0 && static_cast<float>(value) == 0))
          return absl::OutOfRangeError("TI demo joint value exceeds native float range");
      }
      if (command.has_position() && (command.position() < operational_lower_limit_ ||
                                     command.position() > operational_upper_limit_))
        return absl::InvalidArgumentError("TI demo position is outside operational limits");
      if ((command.has_velocity() && command.velocity() < 0) ||
          (command.has_effort() && command.effort() < 0))
        return absl::InvalidArgumentError("TI demo requires nonnegative native speed and effort");
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
      LOG(WARNING) << "No action type set in joint ActionPacket [ID: " << action_packet.action_id()
                   << "]";
      return absl::InvalidArgumentError("ActionPacket requires joint or preset");
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
