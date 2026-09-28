// Implements opt-in diagnostic commands, using JoshuaWireV2Session for all
// on-wire correlation. Neutral payload helpers are independent of wire framing.
#include "robot/board/joshua_wire/serial_v2_validation.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

#include "firmware/common/joshua_wire_commands.h"
#include "robot/board/joshua_wire/joshua_wire_v2_session.h"
#include "utils/status_macros.h"

namespace robot::board::diagnostics {
namespace {
// Largest float below INT32_MAX, safe for the firmware's lroundf -> long target.
constexpr float kMaxMcuPosition = 2147483520.0f;

int WireBoardId(BoardType type) {
  switch (type) {
    case AM243:
      return JW_BOARD_AM243;
    case TEENSY41:
      return JW_BOARD_TEENSY41;
    case ESP32:
      return JW_BOARD_ESP32;
    default:
      return -1;
  }
}

const Channel* SelectedChannel(const Board& board, int index) {
  for (const auto& channel : board.channels()) {
    if (channel.index() == static_cast<uint32_t>(index)) return &channel;
  }
  return nullptr;
}

absl::Status RunSession(JoshuaWireV2Session& session,
                        const Board& board,
                        const SerialV2ValidationOptions& options,
                        std::ostream& output,
                        const std::function<bool()>& cancelled) {
  auto exchange = [&](const jw_command_t& command) -> absl::StatusOr<std::vector<uint8_t>> {
    if (cancelled && cancelled()) return absl::CancelledError("Interrupted; stopping session.");
    return session.Exchange(command);
  };
  auto status_command = [&](uint8_t cmd,
                            uint8_t channel,
                            const uint8_t* payload = nullptr,
                            size_t len = 0) -> absl::Status {
    ABSL_ASSIGN_OR_RETURN(auto response, exchange({cmd, channel, payload, len}));
    jw_status_t status;
    if (jw_decode_status_payload(response.data(), response.size(), &status) != 0) {
      return absl::DataLossError("Malformed status response.");
    }
    if (status != JW_STATUS_OK) return absl::FailedPreconditionError("Firmware rejected command.");
    return absl::OkStatus();
  };
  ABSL_ASSIGN_OR_RETURN(auto response, exchange({JW_CMD_IDENTIFY, JW_CHANNEL_NONE, nullptr, 0}));
  jw_identify_response_t identity{};
  if (jw_decode_identify_payload(response.data(), response.size(), &identity) != 0) {
    return absl::DataLossError("Malformed IDENTIFY response.");
  }
  output << "IDENTIFY board_id=" << identity.board_id
         << " firmware=" << std::string(identity.fw_name, strnlen(identity.fw_name, JW_FW_NAME_LEN))
         << " channels=" << static_cast<int>(identity.n_channels) << '\n';
  if (identity.board_id != WireBoardId(board.board_type())) {
    return absl::FailedPreconditionError("Firmware board identity does not match config.");
  }
  if (options.mode == "handshake") return absl::OkStatus();
  if (options.channel >= identity.n_channels ||
      identity.channel_drives[options.channel] != JW_DRIVE_STEP_DIR) {
    return absl::FailedPreconditionError("Firmware does not expose the selected STEP_DIR channel.");
  }
  const auto& configured = SelectedChannel(board, options.channel)->step_dir();
  jw_configure_step_dir_t config{};
  config.max_pulse_rate_hz = configured.max_pulse_rate_hz();
  config.invert_dir = configured.invert_dir();
  config.enable_active_low = configured.enable_active_low();
  config.step_pin = configured.step_pin();
  config.dir_pin = configured.dir_pin();
  config.enable_pin = configured.enable_pin();
  config.step_pulse_width_us = configured.step_pulse_width_us();
  const auto channel = static_cast<uint8_t>(options.channel);
  uint8_t config_payload[JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN];
  if (jw_encode_configure_step_dir_payload(config_payload, sizeof(config_payload), &config) < 0)
    return absl::InternalError("Cannot encode channel config.");
  ABSL_RETURN_IF_ERROR(
      status_command(JW_CMD_CONFIGURE_CHANNEL, channel, config_payload, sizeof(config_payload)));
  output << "CONFIGURE_CHANNEL OK; no enable sent\n";

  auto feedback = [&]() -> absl::StatusOr<jw_feedback_t> {
    ABSL_ASSIGN_OR_RETURN(auto reply, exchange({JW_CMD_GET_FEEDBACK, channel, nullptr, 0}));
    jw_feedback_t value{};
    if (jw_decode_feedback_payload(reply.data(), reply.size(), &value) != 0 ||
        !std::isfinite(value.position) || !std::isfinite(value.velocity) ||
        std::abs(value.position) > kMaxMcuPosition) {
      return absl::DataLossError("Malformed/non-finite feedback.");
    }
    output << "FEEDBACK position=" << value.position << " velocity=" << value.velocity
           << " faults=" << value.fault_flags << '\n';
    if (value.fault_flags != 0) return absl::FailedPreconditionError("Firmware reports faults.");
    return value;
  };
  ABSL_ASSIGN_OR_RETURN(auto initial, feedback());
  if (options.mode == "configure") return absl::OkStatus();
  // Set a hold target before enabling, so enabling cannot resume an old target.
  auto target = [&](float value) -> absl::Status {
    uint8_t payload[JW_SET_TARGET_PAYLOAD_LEN];
    if (jw_encode_set_target_payload(payload, sizeof(payload), JW_MODE_POSITION, value) < 0)
      return absl::InternalError("Cannot encode target.");
    return status_command(JW_CMD_SET_TARGET, channel, payload, sizeof(payload));
  };
  ABSL_RETURN_IF_ERROR(target(initial.position));
  ABSL_RETURN_IF_ERROR(status_command(JW_CMD_ENABLE, channel));
  output << "ENABLE OK\n";
  ABSL_RETURN_IF_ERROR(target(*options.target_steps));
  output << "SET_TARGET accepted; this does not prove physical motion or arrival\n";
  ABSL_ASSIGN_OR_RETURN(auto final_feedback, feedback());
  (void)final_feedback;
  ABSL_RETURN_IF_ERROR(status_command(JW_CMD_DISABLE, channel));
  output << "DISABLE OK\n";
  return absl::OkStatus();
}
}  // namespace

absl::Status ValidateSerialV2Options(const Board& board, const SerialV2ValidationOptions& options) {
  if (WireBoardId(board.board_type()) < 0 || board.name().empty() ||
      board.protocol() != JOSHUA_WIRE_V2 || board.firmware().min_proto_version() > 2 ||
      board.comm().comm_type() != robot::comm::SERIAL ||
      board.comm().transport_type() != robot::comm::MESSAGE || !board.comm().has_serial_config() ||
      board.comm().serial_config().port().empty() || board.comm().serial_config().baudrate() == 0 ||
      board.comm().serial_config().baudrate() > std::numeric_limits<int>::max()) {
    return absl::InvalidArgumentError(
        "Select a named AM243/TEENSY41/ESP32 board with explicit JOSHUA_WIRE_V2 and SERIAL/MESSAGE "
        "config.");
  }
  if (options.sessions < 1 || options.sessions > 10 || options.settle_ms < 0 ||
      options.settle_ms > 10000) {
    return absl::InvalidArgumentError("sessions must be 1..10; settle_ms must be 0..10000.");
  }
  if (options.mode != "handshake" && options.mode != "configure" && options.mode != "exercise") {
    return absl::InvalidArgumentError("mode must be handshake, configure or exercise.");
  }
  if (options.mode != "exercise" && (options.allow_enable || options.target_steps.has_value())) {
    return absl::InvalidArgumentError("Enable/target options require exercise mode.");
  }
  if (options.mode == "handshake") {
    if (options.channel != -1)
      return absl::InvalidArgumentError("handshake does not select a channel.");
    return absl::OkStatus();
  }
  if (options.channel < 0 || options.channel >= JW_MAX_CHANNELS) {
    return absl::InvalidArgumentError("Select a channel index in 0..7.");
  }
  std::set<uint32_t> indices;
  for (const auto& channel : board.channels()) {
    if (!indices.insert(channel.index()).second)
      return absl::InvalidArgumentError("Duplicate channel index.");
  }
  const auto* channel = SelectedChannel(board, options.channel);
  if (channel == nullptr || channel->drive() != STEP_DIR || !channel->has_step_dir()) {
    return absl::InvalidArgumentError("Selected channel must declare STEP_DIR configuration.");
  }
  const auto& pins = channel->step_dir();
  if (pins.step_pin() > 255 || pins.dir_pin() > 255 || pins.enable_pin() > 255 ||
      pins.step_pin() == pins.dir_pin() || pins.step_pin() == pins.enable_pin() ||
      pins.dir_pin() == pins.enable_pin() || pins.step_pulse_width_us() > 65535 ||
      pins.max_pulse_rate_hz() == 0 || pins.max_pulse_rate_hz() > 1000000) {
    return absl::InvalidArgumentError("Invalid STEP_DIR pins, pulse width or pulse rate.");
  }
  if (options.mode == "exercise" && (!options.allow_enable || !options.target_steps.has_value() ||
                                     !std::isfinite(*options.target_steps) ||
                                     std::abs(*options.target_steps) > kMaxMcuPosition)) {
    return absl::InvalidArgumentError(
        "exercise requires allow_enable and an explicit finite target_steps within the MCU "
        "position range.");
  }
  return absl::OkStatus();
}

absl::Status RunSerialV2Validation(const Board& board,
                                   const SerialV2ValidationOptions& options,
                                   std::shared_ptr<robot::comm::MessageTransport> transport,
                                   std::ostream& output,
                                   std::function<bool()> cancelled) {
  ABSL_RETURN_IF_ERROR(ValidateSerialV2Options(board, options));
  if (!transport) return absl::InvalidArgumentError("Missing message transport.");
  JoshuaWireV2Session session(std::move(transport));
  for (int i = 0; i < options.sessions; ++i) {
    if (cancelled && cancelled()) return absl::CancelledError("Interrupted before reset.");
    ABSL_RETURN_IF_ERROR(session.Open());
    output << "Session " << i + 1 << ": RESET_SESSION OK (outputs disabled, config cleared)\n";
    const auto status = RunSession(session, board, options, output, cancelled);
    const auto stopped = session.Close();  // Also runs after rejected/failed commands.
    output << "ESTOP cleanup: " << stopped << '\n';
    if (!stopped.ok()) {
      return absl::Status(
          stopped.code(),
          std::string(status.message()) +
              "; ESTOP unconfirmed, hardware state unknown: " + std::string(stopped.message()));
    }
    ABSL_RETURN_IF_ERROR(status);
  }
  return absl::OkStatus();
}
}  // namespace robot::board::diagnostics
