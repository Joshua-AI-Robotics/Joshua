#include "robot/board/feetech_bus/feetech_bus_board.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "robot/board/feetech_bus/feetech_protocol.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/comm/interfaces/caller_sized_message_transport.h"
#include "robot/comm/proto/comm.pb.h"
#include "utils/status_macros.h"

namespace robot::board {

struct FeetechBusSharedState {
  std::shared_ptr<robot::comm::CallerSizedMessageTransport> transport;
  // Allows one request/response exchange at a time on the half-duplex bus.
  std::mutex bus_mutex;
  // Protected by bus_mutex. Vendor replies have no transaction identity, so a
  // failed write retires reply consumption for every channel in this generation.
  absl::Status write_fault;

  absl::Status FailWrite(const absl::Status& error) {
    write_fault = absl::FailedPreconditionError(absl::StrCat(
        "Feetech reply stream is uncertain after a failed write; close all consumers and "
        "deliberately restart the runtime before resuming: ",
        error.ToString()));
    return error;
  }
};

namespace {

// One addressable channel on a shared servo bus.
class FeetechBusChannel : public BoardChannel {
 public:
  FeetechBusChannel(std::shared_ptr<FeetechBusSharedState> state,
                    uint8_t servo_id,
                    uint16_t move_time_ms,
                    bool acknowledge_writes)
      : state_(std::move(state)),
        servo_id_(servo_id),
        move_time_ms_(move_time_ms),
        acknowledge_writes_(acknowledge_writes) {}

  absl::Status Enable() override {
    return WriteTorqueEnable(true);
  }

  absl::Status Disable() override {
    return WriteTorqueEnable(false);
  }

  absl::Status SetTarget(TargetMode mode, float value) override {
    switch (mode) {
      case TargetMode::kVelocity: {
        std::lock_guard<std::mutex> bus_lock(state_->bus_mutex);
        if (!state_->write_fault.ok()) return state_->write_fault;
        std::lock_guard<std::mutex> lock(mutex_);
        staged_move_speed_ = ClampToUint16(value);
        return absl::OkStatus();
      }
      case TargetMode::kPosition: {
        uint16_t staged_speed;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          staged_speed = staged_move_speed_;
        }
        std::vector<uint8_t> data = feetech::EncodeUint16Le(ClampToUint16(value));
        auto time_bytes = feetech::EncodeUint16Le(move_time_ms_);
        auto speed_bytes = feetech::EncodeUint16Le(staged_speed);
        data.insert(data.end(), time_bytes.begin(), time_bytes.end());
        data.insert(data.end(), speed_bytes.begin(), speed_bytes.end());
        const auto packet = feetech::BuildWritePacket(servo_id_, feetech::kRegGoalPosition, data);
        return WriteRegister(packet);
      }
      case TargetMode::kTorque:
        // This bus exposes torque enable, but no continuous torque target.
        return absl::UnimplementedError(
            "FEETECH_BUS channel has no continuous torque target; use Enable/Disable.");
    }
    return absl::UnimplementedError("Unknown target mode.");
  }

  absl::StatusOr<ChannelFeedback> ReadFeedback() override {
    const auto request = feetech::BuildReadPacket(servo_id_, feetech::kRegPresentPosition, 2);
    std::vector<uint8_t> response;
    {
      std::lock_guard<std::mutex> lock(state_->bus_mutex);
      if (!state_->write_fault.ok()) return state_->write_fault;
      ABSL_ASSIGN_OR_RETURN(
          response,
          state_->transport->SendAndReceive(request, feetech::kStatusPacketOverheadBytes + 2));
    }
    ABSL_ASSIGN_OR_RETURN(auto params, feetech::ParseStatusPacket(response, servo_id_));
    if (params.size() != 2) {
      return absl::InternalError(absl::StrCat("Servo ",
                                              static_cast<int>(servo_id_),
                                              " present-position read returned ",
                                              params.size(),
                                              " bytes, expected 2."));
    }
    ChannelFeedback feedback;
    feedback.position = static_cast<float>(feetech::DecodeUint16Le(params[0], params[1]));
    return feedback;
  }

 private:
  static uint16_t ClampToUint16(float value) {
    return static_cast<uint16_t>(std::clamp(std::lround(value), 0L, 65535L));
  }

  absl::Status WriteTorqueEnable(bool enable) {
    const auto packet = feetech::BuildWritePacket(
        servo_id_, feetech::kRegTorqueEnable, {static_cast<uint8_t>(enable ? 1 : 0)});
    return WriteRegister(packet, !enable);
  }

  absl::Status WriteRegister(const std::vector<uint8_t>& packet, bool disabling = false) {
    // A late write acknowledgment must not become the next channel's feedback.
    // Keep the whole request/reply under the same lock as position reads. Never
    // retry: a failed exchange can still have executed the write on the servo.
    std::lock_guard<std::mutex> lock(state_->bus_mutex);
    if (!state_->write_fault.ok()) {
      if (!disabling) return state_->write_fault;
      // Preserve the stop path without reading an ACK that could belong to the
      // failed operation. OS acceptance is never reported as confirmed disable.
      auto sent = state_->transport->Send(packet);
      if (!sent.ok())
        return absl::Status(sent.code(),
                            absl::StrCat("Best-effort torque disable is unconfirmed: ",
                                         sent.ToString(),
                                         "; ",
                                         state_->write_fault.message()));
      return absl::FailedPreconditionError(
          absl::StrCat("Best-effort torque-disable bytes sent; outcome unconfirmed. ",
                       state_->write_fault.message()));
    }
    if (!acknowledge_writes_) {
      auto sent = state_->transport->Send(packet);
      return sent.ok() ? sent : state_->FailWrite(sent);
    }
    auto response = state_->transport->SendAndReceive(packet, feetech::kStatusPacketOverheadBytes);
    if (!response.ok()) return state_->FailWrite(response.status());
    auto params = feetech::ParseStatusPacket(*response, servo_id_);
    if (!params.ok()) return state_->FailWrite(params.status());
    if (!params->empty())
      return state_->FailWrite(absl::InternalError("Feetech write acknowledgment has parameters"));
    return absl::OkStatus();
  }

  std::shared_ptr<FeetechBusSharedState> state_;
  const uint8_t servo_id_;
  const uint16_t move_time_ms_;
  const bool acknowledge_writes_;
  std::mutex mutex_;
  uint16_t staged_move_speed_ = 0;
};

absl::Status ValidateConfig(const robot::board::Board& config) {
  if (config.protocol() != BOARD_PROTOCOL_UNSPECIFIED) {
    return absl::InvalidArgumentError("FEETECH_BUS does not speak JoshuaWire.");
  }
  if (config.board_type() != robot::board::BoardType::FEETECH_BUS) {
    return absl::InvalidArgumentError(
        absl::StrCat("Board '", config.name(), "' is not a FEETECH_BUS board."));
  }
  if (config.comm().comm_type() != robot::comm::CommType::SERIAL ||
      !config.comm().has_serial_config()) {
    return absl::InvalidArgumentError(
        absl::StrCat("FEETECH_BUS board '", config.name(), "' requires SERIAL comm config."));
  }
  if (config.comm().transport_type() != robot::comm::TransportType::MESSAGE) {
    return absl::InvalidArgumentError(
        absl::StrCat("FEETECH_BUS board '", config.name(), "' requires MESSAGE transport."));
  }
  if (config.comm().serial_config().message_adapter() != robot::comm::CALLER_SIZED) {
    return absl::InvalidArgumentError(
        absl::StrCat("FEETECH_BUS board '", config.name(), "' requires CALLER_SIZED messages."));
  }
  if (config.channels_size() == 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("FEETECH_BUS board '", config.name(), "' declares no channels."));
  }
  std::set<uint32_t> seen_servo_ids;
  for (const auto& channel : config.channels()) {
    if (channel.drive() != robot::board::DriveInterface::SERVO_BUS_UART) {
      return absl::InvalidArgumentError(absl::StrCat("FEETECH_BUS board '",
                                                     config.name(),
                                                     "' channel ",
                                                     channel.index(),
                                                     " must use the SERVO_BUS_UART drive."));
    }
    if (!channel.has_servo_bus() || channel.servo_bus().servo_id() == 0) {
      return absl::InvalidArgumentError(absl::StrCat("FEETECH_BUS board '",
                                                     config.name(),
                                                     "' channel ",
                                                     channel.index(),
                                                     " has no servo_bus.servo_id."));
    }
    if (!seen_servo_ids.insert(channel.servo_bus().servo_id()).second) {
      return absl::InvalidArgumentError(absl::StrCat("FEETECH_BUS board '",
                                                     config.name(),
                                                     "' declares servo_id ",
                                                     channel.servo_bus().servo_id(),
                                                     " more than once."));
    }
  }
  return absl::OkStatus();
}

// Verifies that the configured servo responds and exposes its model register.
absl::StatusOr<bool> IdentifyServo(FeetechBusSharedState& state,
                                   const std::string& board_name,
                                   uint8_t servo_id) {
  std::lock_guard<std::mutex> lock(state.bus_mutex);

  const auto ping = feetech::BuildPingPacket(servo_id);
  ABSL_ASSIGN_OR_RETURN(auto ping_response,
                        state.transport->SendAndReceive(ping, feetech::kStatusPacketOverheadBytes));
  auto ping_status = feetech::ParseStatusPacket(ping_response, servo_id);
  if (!ping_status.ok()) {
    return absl::UnavailableError(absl::StrCat("Board '",
                                               board_name,
                                               "': no response from servo ",
                                               static_cast<int>(servo_id),
                                               " on the Feetech bus; check wiring/servo ID. ",
                                               ping_status.status().message()));
  }

  const auto read_model = feetech::BuildReadPacket(servo_id, feetech::kRegModelNumber, 2);
  ABSL_ASSIGN_OR_RETURN(
      auto model_response,
      state.transport->SendAndReceive(read_model, feetech::kStatusPacketOverheadBytes + 2));
  ABSL_RETURN_IF_ERROR(feetech::ParseStatusPacket(model_response, servo_id).status());
  const auto read_level = feetech::BuildReadPacket(servo_id, feetech::kRegStatusReturnLevel, 1);
  ABSL_ASSIGN_OR_RETURN(
      auto level_response,
      state.transport->SendAndReceive(read_level, feetech::kStatusPacketOverheadBytes + 1));
  ABSL_ASSIGN_OR_RETURN(auto level, feetech::ParseStatusPacket(level_response, servo_id));
  if (level.size() != 1 || level[0] > 1) {
    return absl::FailedPreconditionError(absl::StrCat(
        "Board '", board_name, "': unsupported status return level for servo ", servo_id));
  }
  return level[0] == 1;
}

}  // namespace

absl::Status FeetechBusBoard::Init(const robot::board::Board& config) {
  if (initialized_) {
    return absl::FailedPreconditionError(
        absl::StrCat("FEETECH_BUS board '", config.name(), "' is already initialized."));
  }
  ABSL_RETURN_IF_ERROR(ValidateConfig(config));

  auto state = std::make_shared<FeetechBusSharedState>();
  ABSL_ASSIGN_OR_RETURN(auto comm, robot::comm::CommFactory::CreateComm(config.comm()));
  ABSL_ASSIGN_OR_RETURN(auto message,
                        robot::comm::GetCommTransport<robot::comm::MessageTransport>(comm));
  ABSL_ASSIGN_OR_RETURN(state->transport, robot::comm::GetCallerSizedMessageTransport(message));

  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels;
  for (const auto& channel_config : config.channels()) {
    if (channels.count(channel_config.index()) > 0) {
      return absl::InvalidArgumentError(absl::StrCat("Board '",
                                                     config.name(),
                                                     "' declares channel index ",
                                                     channel_config.index(),
                                                     " more than once."));
    }
    const auto servo_id = static_cast<uint8_t>(channel_config.servo_bus().servo_id());
    ABSL_ASSIGN_OR_RETURN(bool acknowledge_writes, IdentifyServo(*state, config.name(), servo_id));
    channels[channel_config.index()] = std::make_shared<FeetechBusChannel>(
        state,
        servo_id,
        static_cast<uint16_t>(channel_config.servo_bus().move_time_ms()),
        acknowledge_writes);
  }

  config_ = config;
  state_ = std::move(state);
  channels_ = std::move(channels);
  initialized_ = true;
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<BoardChannel>> FeetechBusBoard::OpenChannel(uint32_t index) {
  if (!initialized_) {
    return absl::FailedPreconditionError("FEETECH_BUS board is not initialized.");
  }
  auto it = channels_.find(index);
  if (it == channels_.end()) {
    return absl::NotFoundError(absl::StrCat("Board '",
                                            config_.name(),
                                            "' has no channel ",
                                            index,
                                            "; declare it in the board's channels{}."));
  }
  return it->second;
}

absl::Status FeetechBusBoard::Teardown() {
  channels_.clear();
  state_.reset();
  initialized_ = false;
  return absl::OkStatus();
}

}  // namespace robot::board
