#include "robot/action/motors/drivers/am243_ethercat_driver.h"

#include <glog/logging.h>

#include <string>

#include "robot/comm/ethercat/ethercat_status.h"

namespace robot::action {

namespace {

absl::Status UnimplementedPdoMappingStatus(const std::string& command) {
  return absl::Status(absl::StatusCode::kUnimplemented,
                      "AM243 EtherCAT PDO mapping is not implemented for " + command);
}

}  // namespace

Am243EthercatDriver::Am243EthercatDriver(
    const std::shared_ptr<robot::comm::ethercat::EthercatTransport>& ethercat,
    const robot::action::Actuator& action_config)
    : ethercat_(ethercat), action_config_(action_config) {
  physical_lower_limit_ = action_config.physical_lower_limit();
  physical_upper_limit_ = action_config.physical_upper_limit();
  operational_lower_limit_ = action_config.operational_lower_limit();
  operational_upper_limit_ = action_config.operational_upper_limit();

  if (action_config.has_am243_ethercat_config()) {
    idle_position_ = action_config.am243_ethercat_config().idle_position();
  }

  id_ = GetId();
  LOG(INFO) << "Am243EthercatDriver actuator ID: " << action_config_.id() << " initialized";
}

absl::Status Am243EthercatDriver::Init() {
  if (ethercat_ == nullptr) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "AM243 EtherCAT driver requires an EtherCAT transport");
  }

  if (!action_config_.has_am243_ethercat_config()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "AM243 EtherCAT actuator has no am243_ethercat_config");
  }

  const auto& config = action_config_.am243_ethercat_config();
  pdo_region_.slave_index = static_cast<uint16_t>(config.slave_index());
  pdo_region_.output_offset_bytes = config.output_offset_bytes();
  pdo_region_.input_offset_bytes = config.input_offset_bytes();
  pdo_region_.output_size_bytes = config.output_size_bytes();
  pdo_region_.input_size_bytes = config.input_size_bytes();

  if (pdo_region_.output_size_bytes == 0 && pdo_region_.input_size_bytes == 0) {
    auto region_or = ethercat_->GetPdoRegion(pdo_region_.slave_index);
    if (!region_or.ok()) {
      return region_or.status();
    }
    pdo_region_ = *region_or;
  }

  return robot::comm::ethercat::ValidatePdoRegion(pdo_region_);
}

std::string Am243EthercatDriver::GetId() {
  if (!action_config_.actuator_name().empty()) {
    return "am243_ethercat_driver_" + action_config_.actuator_name();
  }
  return "am243_ethercat_driver_" + std::to_string(action_config_.id());
}

absl::Status Am243EthercatDriver::SetAction(const robot::action::ActionPacket& action_packet) {
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
          LOG(WARNING) << "Unknown AM243 preset command: " << action_packet.preset();
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
        LOG(WARNING) << "dc is not supported on AM243 EtherCAT actuator, ignoring";
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
      LOG(WARNING) << "dc is not supported on AM243 EtherCAT actuator, ignoring";
      return absl::OkStatus();
    case robot::action::ActionPacket::ACTION_TYPE_NOT_SET:
    default:
      LOG(WARNING) << "No action type set in AM243 ActionPacket [ID: " << action_packet.action_id()
                   << "]";
      return absl::OkStatus();
  }
}

absl::Status Am243EthercatDriver::SetSpeed(float value) {
  if (value < 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "AM243 speed must be non-negative");
  }
  speed_ = value;
  return UnimplementedPdoMappingStatus("speed");
}

absl::Status Am243EthercatDriver::SetPosition(float angle) {
  if (angle < operational_lower_limit_ || angle > operational_upper_limit_) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "AM243 position is outside operational limits");
  }
  return UnimplementedPdoMappingStatus("position");
}

absl::Status Am243EthercatDriver::SetTorque(float torque) {
  if (torque < 0.0f) {
    return absl::Status(absl::StatusCode::kInvalidArgument, "AM243 torque must be non-negative");
  }
  torque_ = torque;
  return UnimplementedPdoMappingStatus("torque");
}

absl::Status Am243EthercatDriver::SetMiddlePosition() {
  return SetPosition((operational_lower_limit_ + operational_upper_limit_) / 2.0f);
}

absl::Status Am243EthercatDriver::SetIdlePosition() {
  return SetPosition(idle_position_);
}

absl::Status Am243EthercatDriver::Teardown() {
  auto idle_status = SetIdlePosition();
  if (!idle_status.ok() && idle_status.code() != absl::StatusCode::kUnimplemented) {
    return idle_status;
  }

  auto torque_status = SetTorque(0.0f);
  if (!torque_status.ok() && torque_status.code() != absl::StatusCode::kUnimplemented) {
    return torque_status;
  }

  return absl::OkStatus();
}

}  // namespace robot::action
