#include "robot/board/joshua_wire/joshua_wire_board.h"

#include <set>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "firmware/common/joshua_wire_v1.h"
#include "robot/board/joshua_wire/joshua_wire_v2_session.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/comm/interfaces/legacy_message_transport.h"
#include "utils/status_macros.h"

namespace robot::board {

// Board-local wire-version selection. Channels operate on neutral commands;
// only the v1 branch encodes/decodes v1 envelopes. Comm remains byte-oriented.
class JoshuaWireCommandClient {
 public:
  JoshuaWireCommandClient(std::shared_ptr<FrameTransport> transport, bool v2)
      : transport_(std::move(transport)) {
    if (v2) session_ = std::make_unique<JoshuaWireV2Session>(transport_);
  }
  absl::Status Open() {
    return session_ ? session_->Open() : absl::OkStatus();
  }
  absl::Status Close() {
    return session_ ? session_->Close() : absl::OkStatus();
  }
  uint32_t protocol_version() const {
    return session_ ? 2 : 1;
  }
  absl::StatusOr<std::vector<uint8_t>> Exchange(const jw_command_t& command) {
    if (session_) return session_->Exchange(command);
    ABSL_ASSIGN_OR_RETURN(auto legacy, robot::comm::GetLegacyMessageTransport(transport_));
    if (command.payload_len > JW1_MAX_PAYLOAD_LEN)
      return absl::InvalidArgumentError("JoshuaWire v1 payload too large.");
    uint8_t bytes[JW1_MAX_FRAME_LEN];
    const int len = jw1_encode_frame(bytes,
                                     sizeof(bytes),
                                     command.cmd,
                                     command.channel,
                                     command.payload,
                                     static_cast<uint8_t>(command.payload_len));
    if (len < 0) return absl::InvalidArgumentError("Invalid JoshuaWire v1 command.");
    size_t expected = JW_STATUS_RESPONSE_PAYLOAD_LEN;
    if (command.cmd == JW_CMD_IDENTIFY) expected = JW_IDENTIFY_RESPONSE_PAYLOAD_LEN;
    if (command.cmd == JW_CMD_GET_FEEDBACK) expected = JW_FEEDBACK_RESPONSE_PAYLOAD_LEN;
    ABSL_ASSIGN_OR_RETURN(
        auto response,
        legacy->SendAndReceive(std::vector<uint8_t>(bytes, bytes + len), JW1_FRAME_LEN(expected)));
    jw1_frame_t frame;
    if (jw1_decode_frame(response.data(), response.size(), &frame) != 0 ||
        frame.cmd != command.cmd || frame.channel != command.channel)
      return absl::DataLossError("Malformed/mismatched JoshuaWire v1 response.");
    return std::vector<uint8_t>(frame.payload, frame.payload + frame.payload_len);
  }

 private:
  std::shared_ptr<FrameTransport> transport_;
  std::unique_ptr<JoshuaWireV2Session> session_;
};

namespace {

absl::Status JwStatusToAbsl(jw_status_t status, const std::string& what) {
  switch (status) {
    case JW_STATUS_OK:
      return absl::OkStatus();
    case JW_STATUS_UNSUPPORTED:
      return absl::UnimplementedError(absl::StrCat(what, ": firmware reports unsupported."));
    case JW_STATUS_ERROR:
    default:
      return absl::InternalError(absl::StrCat(what, ": firmware reports error."));
  }
}

// robot.board.DriveInterface -> jw_drive_t, value-for-value (mirrors the
// comment on jw_drive_t in joshua_wire_commands.h). Used to cross-check IDENTIFY's
// reported per-channel drive against what config declares, generically —
// adding a wire-side drive here (there already are PWM_DC/SERVO_BUS_UART/
// CAN/PDO_JOINT slots reserved) needs no change to IdentifyAndValidate.
absl::StatusOr<jw_drive_t> ToWireDrive(robot::board::DriveInterface drive) {
  switch (drive) {
    case robot::board::DriveInterface::STEP_DIR:
      return JW_DRIVE_STEP_DIR;
    case robot::board::DriveInterface::PWM_DC:
      return JW_DRIVE_PWM_DC;
    case robot::board::DriveInterface::SERVO_BUS_UART:
      return JW_DRIVE_SERVO_BUS_UART;
    case robot::board::DriveInterface::CAN:
      return JW_DRIVE_CAN;
    case robot::board::DriveInterface::PDO_JOINT:
      return JW_DRIVE_PDO_JOINT;
    default:
      return absl::InvalidArgumentError(absl::StrCat("DriveInterface ",
                                                     robot::board::DriveInterface_Name(drive),
                                                     " has no JoshuaWire wire-drive mapping."));
  }
}

// One addressable channel, independent of the selected wire envelope.
class JoshuaWireChannel : public BoardChannel {
 public:
  JoshuaWireChannel(std::shared_ptr<JoshuaWireCommandClient> commands, uint8_t channel_index)
      : commands_(std::move(commands)), channel_index_(channel_index) {}

  absl::Status Enable() override {
    return SendExpectStatus(JW_CMD_ENABLE, nullptr, 0, "Enable");
  }
  absl::Status Disable() override {
    return SendExpectStatus(JW_CMD_DISABLE, nullptr, 0, "Disable");
  }
  absl::Status SetTarget(TargetMode mode, float value) override {
    if (mode == TargetMode::kTorque)
      return absl::UnimplementedError("JoshuaWire channel has no torque target (open-loop drive).");
    const jw_mode_t wire_mode = mode == TargetMode::kPosition ? JW_MODE_POSITION : JW_MODE_VELOCITY;
    uint8_t payload[JW_SET_TARGET_PAYLOAD_LEN];
    const int len = jw_encode_set_target_payload(payload, sizeof(payload), wire_mode, value);
    if (len < 0) return absl::InternalError("Cannot encode SET_TARGET payload.");
    return SendExpectStatus(JW_CMD_SET_TARGET, payload, len, "SetTarget");
  }
  absl::StatusOr<ChannelFeedback> ReadFeedback() override {
    ABSL_ASSIGN_OR_RETURN(auto response,
                          commands_->Exchange({JW_CMD_GET_FEEDBACK, channel_index_, nullptr, 0}));
    jw_feedback_t feedback;
    if (jw_decode_feedback_payload(response.data(), response.size(), &feedback) != 0)
      return absl::InternalError("Malformed GET_FEEDBACK response payload.");
    ChannelFeedback out;
    out.position = feedback.position;
    out.velocity = feedback.velocity;
    out.fault_flags = feedback.fault_flags;
    return out;
  }

 private:
  absl::Status SendExpectStatus(uint8_t cmd,
                                const uint8_t* payload,
                                size_t len,
                                const std::string& what) {
    ABSL_ASSIGN_OR_RETURN(auto response, commands_->Exchange({cmd, channel_index_, payload, len}));
    jw_status_t status;
    if (jw_decode_status_payload(response.data(), response.size(), &status) != 0)
      return absl::InternalError(absl::StrCat("Malformed ", what, " response payload."));
    return JwStatusToAbsl(status, what);
  }
  std::shared_ptr<JoshuaWireCommandClient> commands_;
  const uint8_t channel_index_;
};

// STEP_DIR is the only configured drive payload implemented today. Further
// drives add payload helpers and dispatch here, not new per-board wire codecs.
absl::Status ConfigureChannel(JoshuaWireCommandClient& commands,
                              const robot::board::Channel& channel) {
  if (channel.drive_config_case() != robot::board::Channel::kStepDir)
    return absl::UnimplementedError("Unsupported JoshuaWire channel configuration.");
  jw_configure_step_dir_t config{};
  config.max_pulse_rate_hz = channel.step_dir().max_pulse_rate_hz();
  config.invert_dir = channel.step_dir().invert_dir() ? 1 : 0;
  config.enable_active_low = channel.step_dir().enable_active_low() ? 1 : 0;
  config.step_pin = static_cast<uint8_t>(channel.step_dir().step_pin());
  config.dir_pin = static_cast<uint8_t>(channel.step_dir().dir_pin());
  config.enable_pin = static_cast<uint8_t>(channel.step_dir().enable_pin());
  config.step_pulse_width_us = static_cast<uint16_t>(channel.step_dir().step_pulse_width_us());
  uint8_t payload[JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN];
  const int len = jw_encode_configure_step_dir_payload(payload, sizeof(payload), &config);
  if (len < 0) return absl::InternalError("Cannot encode CONFIGURE_CHANNEL payload.");
  ABSL_ASSIGN_OR_RETURN(auto response,
                        commands.Exchange({JW_CMD_CONFIGURE_CHANNEL,
                                           static_cast<uint8_t>(channel.index()),
                                           payload,
                                           static_cast<size_t>(len)}));
  jw_status_t status;
  if (jw_decode_status_payload(response.data(), response.size(), &status) != 0)
    return absl::InternalError("Malformed CONFIGURE_CHANNEL response payload.");
  return JwStatusToAbsl(status, absl::StrCat("CONFIGURE_CHANNEL(", channel.index(), ")"));
}

}  // namespace

absl::Status JoshuaWireBoard::ValidateComm(const robot::comm::Comm& comm,
                                           const std::string& board_name) const {
  if (comm.comm_type() != robot::comm::CommType::SERIAL || !comm.has_serial_config()) {
    return absl::InvalidArgumentError(
        absl::StrCat("Board '", board_name, "' requires SERIAL comm config."));
  }
  if (comm.transport_type() != robot::comm::TransportType::MESSAGE) {
    return absl::InvalidArgumentError(
        absl::StrCat("Board '", board_name, "' requires MESSAGE transport."));
  }
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<FrameTransport>> JoshuaWireBoard::CreateTransport(
    const robot::comm::Comm& comm) const {
  ABSL_ASSIGN_OR_RETURN(auto transport, robot::comm::CommFactory::CreateComm(comm));
  return robot::comm::GetCommTransport<robot::comm::MessageTransport>(transport);
}

absl::Status JoshuaWireBoard::ValidateConfig(const robot::board::Board& config) const {
  if (config.protocol() != BOARD_PROTOCOL_UNSPECIFIED && config.protocol() != JOSHUA_WIRE_V1 &&
      config.protocol() != JOSHUA_WIRE_V2) {
    return absl::InvalidArgumentError("Unsupported JoshuaWire board protocol.");
  }
  const std::string type_name = robot::board::BoardType_Name(expected_board_type_);
  if (config.board_type() != expected_board_type_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Board '", config.name(), "' is not a ", type_name, " board."));
  }
  ABSL_RETURN_IF_ERROR(ValidateComm(config.comm(), config.name()));
  if (!config.has_firmware()) {
    return absl::InvalidArgumentError(absl::StrCat(
        type_name, " board '", config.name(), "' requires a firmware{} spec for IDENTIFY."));
  }
  if (config.channels_size() == 0) {
    return absl::InvalidArgumentError(
        absl::StrCat(type_name, " board '", config.name(), "' declares no channels."));
  }
  if (config.channels_size() > JW_MAX_CHANNELS) {
    return absl::InvalidArgumentError(absl::StrCat(type_name,
                                                   " board '",
                                                   config.name(),
                                                   "' declares more channels than JoshuaWire "
                                                   "supports (",
                                                   JW_MAX_CHANNELS,
                                                   ")."));
  }
  std::set<uint32_t> seen_indices;
  for (const auto& channel : config.channels()) {
    // STEP_DIR is the only drive this codec's CONFIGURE_CHANNEL encoder
    // implements today (see ConfigureChannel below) — not a per-board
    // limit, so this check lives here rather than in a subclass.
    if (channel.drive() != robot::board::DriveInterface::STEP_DIR ||
        channel.drive_config_case() != robot::board::Channel::kStepDir) {
      return absl::InvalidArgumentError(
          absl::StrCat(type_name,
                       " board '",
                       config.name(),
                       "' channel ",
                       channel.index(),
                       " declares drive ",
                       robot::board::DriveInterface_Name(channel.drive()),
                       ", but JoshuaWire's CONFIGURE_CHANNEL encoder only supports "
                       "STEP_DIR today."));
    }
    for (uint32_t pin : {channel.step_dir().step_pin(),
                         channel.step_dir().dir_pin(),
                         channel.step_dir().enable_pin()}) {
      if (pin > 255) {
        return absl::InvalidArgumentError(
            absl::StrCat(type_name,
                         " board '",
                         config.name(),
                         "' channel ",
                         channel.index(),
                         " declares a pin number ",
                         pin,
                         " that doesn't fit an MCU GPIO pin (max 255)."));
      }
    }
    if (channel.step_dir().step_pulse_width_us() > 65535) {
      return absl::InvalidArgumentError(
          absl::StrCat(type_name,
                       " board '",
                       config.name(),
                       "' channel ",
                       channel.index(),
                       " declares step_pulse_width_us ",
                       channel.step_dir().step_pulse_width_us(),
                       " that doesn't fit the wire field (max 65535)."));
    }
    if (channel.index() >= JW_MAX_CHANNELS) {
      return absl::InvalidArgumentError(absl::StrCat(type_name,
                                                     " board '",
                                                     config.name(),
                                                     "' channel index ",
                                                     channel.index(),
                                                     " exceeds JoshuaWire's max channel "
                                                     "index (",
                                                     JW_MAX_CHANNELS - 1,
                                                     "); it must match the firmware channel "
                                                     "table's array position."));
    }
    if (!seen_indices.insert(channel.index()).second) {
      return absl::InvalidArgumentError(absl::StrCat(type_name,
                                                     " board '",
                                                     config.name(),
                                                     "' declares channel index ",
                                                     channel.index(),
                                                     " more than once."));
    }
  }
  return absl::OkStatus();
}

// Validates the board identity, protocol version, and channel capabilities
// reported by firmware against configuration.
absl::Status JoshuaWireBoard::IdentifyAndValidate(JoshuaWireCommandClient& commands,
                                                  const robot::board::Board& config) const {
  ABSL_ASSIGN_OR_RETURN(auto response,
                        commands.Exchange({JW_CMD_IDENTIFY, JW_CHANNEL_NONE, nullptr, 0}));
  const uint32_t protocol_version = commands.protocol_version();
  if (protocol_version < config.firmware().min_proto_version()) {
    return absl::FailedPreconditionError(absl::StrCat("Board '",
                                                      config.name(),
                                                      "': firmware proto_ver ",
                                                      protocol_version,
                                                      " is older than the configured "
                                                      "min_proto_version ",
                                                      config.firmware().min_proto_version(),
                                                      "."));
  }

  jw_identify_response_t identify;
  if (jw_decode_identify_payload(response.data(), response.size(), &identify) != 0) {
    return absl::UnavailableError(
        absl::StrCat("Board '", config.name(), "': malformed IDENTIFY payload."));
  }
  // Stable wire IDs (distinct from protobuf values for ESP32) let the host
  // catch "wrong device on this port" — e.g. a stale/
  // re-enumerated serial path now pointing at a different board type —
  // instead of silently proceeding as long as channel shapes happen to
  // match.
  if (identify.board_id != expected_wire_board_id_) {
    return absl::FailedPreconditionError(absl::StrCat("Board '",
                                                      config.name(),
                                                      "': IDENTIFY reports board_id ",
                                                      static_cast<int>(identify.board_id),
                                                      ", expected ",
                                                      static_cast<int>(expected_wire_board_id_),
                                                      ". Wrong device on this port?"));
  }

  for (const auto& channel : config.channels()) {
    if (channel.index() >= identify.n_channels) {
      return absl::FailedPreconditionError(absl::StrCat("Board '",
                                                        config.name(),
                                                        "': config declares channel ",
                                                        channel.index(),
                                                        " but firmware IDENTIFY reports only ",
                                                        identify.n_channels,
                                                        " channels."));
    }
    ABSL_ASSIGN_OR_RETURN(const jw_drive_t expected_drive, ToWireDrive(channel.drive()));
    if (identify.channel_drives[channel.index()] != expected_drive) {
      return absl::FailedPreconditionError(
          absl::StrCat("Board '",
                       config.name(),
                       "': config declares channel ",
                       channel.index(),
                       " as ",
                       robot::board::DriveInterface_Name(channel.drive()),
                       ", but firmware IDENTIFY reports a "
                       "different drive."));
    }
  }
  return absl::OkStatus();
}

absl::Status JoshuaWireBoard::Init(const robot::board::Board& config) {
  if (initialized_) {
    return absl::FailedPreconditionError(
        absl::StrCat("Board '", config.name(), "' is already initialized."));
  }
  ABSL_RETURN_IF_ERROR(ValidateConfig(config));

  ABSL_ASSIGN_OR_RETURN(auto transport, CreateTransport(config.comm()));

  auto commands = std::make_shared<JoshuaWireCommandClient>(std::move(transport),
                                                            config.protocol() == JOSHUA_WIRE_V2);
  ABSL_RETURN_IF_ERROR(commands->Open());
  ABSL_RETURN_IF_ERROR(IdentifyAndValidate(*commands, config));

  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels;
  for (const auto& channel_config : config.channels()) {
    ABSL_RETURN_IF_ERROR(ConfigureChannel(*commands, channel_config));
    channels[channel_config.index()] =
        std::make_shared<JoshuaWireChannel>(commands, static_cast<uint8_t>(channel_config.index()));
  }

  config_ = config;
  commands_ = std::move(commands);
  channels_ = std::move(channels);
  initialized_ = true;
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<BoardChannel>> JoshuaWireBoard::OpenChannel(uint32_t index) {
  if (!initialized_) {
    return absl::FailedPreconditionError("Board is not initialized.");
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

absl::Status JoshuaWireBoard::Teardown() {
  const auto status = commands_ ? commands_->Close() : absl::OkStatus();
  channels_.clear();
  commands_.reset();
  initialized_ = false;
  return status;
}

}  // namespace robot::board
