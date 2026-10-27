// Implements opt-in diagnostic commands, using JoshuaWireSession for all
// on-wire correlation. Neutral payload helpers are independent of wire framing.
#include "robot/board/joshua_wire/serial_validation.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <vector>

#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "robot/board/joshua_wire/joshua_wire_session.h"
#include "robot/comm/serial/serial_config_validation.h"
#include "utils/status_macros.h"

namespace robot::board::diagnostics {
namespace {
// Largest float below INT32_MAX, safe for the firmware's lroundf -> long target.
constexpr float kMaxMcuPosition = 2147483520.0f;

bool SupportedBoard(BoardType type) {
  return type == AM243 || type == TEENSY41 || type == ESP32;
}
const Channel* SelectedChannel(const Board& board, int index) {
  for (const auto& channel : board.channels()) {
    if (channel.index() == static_cast<uint32_t>(index)) return &channel;
  }
  return nullptr;
}

absl::Status RunSession(JoshuaWireSession& session,
                        const Board& board,
                        const SerialValidationOptions& options,
                        std::ostream& output,
                        const std::function<bool()>& cancelled) {
  auto exchange = [&](const jw_command_t& command) -> absl::StatusOr<std::vector<uint8_t>> {
    if (cancelled && cancelled()) return absl::CancelledError("Interrupted; stopping session.");
    ABSL_ASSIGN_OR_RETURN(auto bytes, session.Exchange(command));
    jw_reply_t reply{};
    if (jw_decode_reply_payload(bytes.data(), bytes.size(), &reply) != JW_RESULT_OK)
      return absl::DataLossError("Malformed common reply.");
    if (reply.status != JW_STATUS_OK || reply.execution != JW_EXECUTION_COMPLETE)
      return absl::FailedPreconditionError("Firmware rejected operation or completion is pending.");
    return std::vector<uint8_t>(reply.data, reply.data + reply.data_len);
  };
  auto status_command = [&](uint8_t cmd,
                            uint8_t channel,
                            const uint8_t* payload = nullptr,
                            size_t len = 0) -> absl::Status {
    uint8_t bytes[JW_MAX_PAYLOAD_LEN];
    jw_profile_request_t request{JW_PROFILE_ACTUATOR, 1, 0, cmd, payload, len};
    int size = jw_encode_profile_request(bytes, sizeof(bytes), &request);
    if (size < 0) return absl::InvalidArgumentError("Invalid actuator request.");
    return exchange({0, 0, JW_CMD_PROFILE, channel, bytes, static_cast<size_t>(size)}).status();
  };
  ABSL_ASSIGN_OR_RETURN(auto response,
                        exchange({0, 0, JW_CMD_IDENTIFY, JW_CHANNEL_NONE, nullptr, 0}));
  jw_identify_response_t identity{};
  if (jw_decode_identify_payload(response.data(), response.size(), &identity) != JW_RESULT_OK) {
    return absl::DataLossError("Malformed IDENTIFY response.");
  }
  output << "IDENTIFY vendor=" << identity.vendor_id << " product=" << identity.product_id
         << " firmware=" << std::string(identity.fw_name, strnlen(identity.fw_name, JW_FW_NAME_LEN))
         << " functions=" << identity.function_count << '\n';
  if (identity.protocol_min > JW_PROTO_VERSION || identity.protocol_max < JW_PROTO_VERSION ||
      !(identity.supported_planes & JW_PLANE_MESSAGE) ||
      !(identity.safety_flags & JW_SAFETY_BOARD_ESTOP))
    return absl::FailedPreconditionError("Incompatible firmware protocol/plane/safety.");
  if (options.mode == "handshake") return absl::OkStatus();
  jw_function_descriptor_t selected{};
  uint16_t cursor = 0;
  uint32_t generation = 0;
  size_t count = 0;
  int previous = -1;
  do {
    uint8_t request[6];
    for (int i = 0; i < 4; ++i) request[i] = generation >> (8 * i);
    request[4] = cursor;
    request[5] = cursor >> 8;
    ABSL_ASSIGN_OR_RETURN(auto page_bytes,
                          exchange({0, 0, JW_CMD_DISCOVER, JW_CHANNEL_NONE, request, 6}));
    jw_discovery_page_t page{};
    if (jw_decode_discovery_page(page_bytes.data(), page_bytes.size(), &page) != JW_RESULT_OK ||
        page.generation != identity.discovery_generation || page.total != identity.function_count ||
        (page.next_cursor != JW_CURSOR_END &&
         (!page.count || page.next_cursor != cursor + page.count)))
      return absl::DataLossError("Inconsistent discovery.");
    for (int i = 0; i < page.count; ++i) {
      if (page.functions[i].function_id <= previous)
        return absl::DataLossError("Unsorted discovery IDs.");
      previous = page.functions[i].function_id;
      ++count;
      if (previous == options.channel) selected = page.functions[i];
    }
    cursor = page.next_cursor;
    generation = page.generation;
  } while (cursor != JW_CURSOR_END && count <= JW_MAX_FUNCTIONS);
  if (count != identity.function_count || selected.profile_id != JW_PROFILE_ACTUATOR ||
      selected.profile_major != 1 || (selected.operations & 31) != 31)
    return absl::FailedPreconditionError("Selected actuator profile is unavailable.");
  ABSL_ASSIGN_OR_RETURN(
      auto metadata,
      exchange(
          {0, 0, JW_CMD_DESCRIBE_FUNCTION, static_cast<uint8_t>(options.channel), nullptr, 0}));
  jw_actuator_schema_t schema{};
  if (metadata.size() != JW_FUNCTION_DESCRIPTOR_LEN + JW_ACTUATOR_SCHEMA_LEN ||
      jw_decode_actuator_schema(metadata.data() + JW_FUNCTION_DESCRIPTOR_LEN,
                                JW_ACTUATOR_SCHEMA_LEN,
                                &schema) != JW_RESULT_OK ||
      schema.drive != JW_DRIVE_STEP_DIR || schema.position_unit != JW_UNIT_NATIVE)
    return absl::FailedPreconditionError("Unsupported STEP/DIR schema.");
  ABSL_ASSIGN_OR_RETURN(auto check, exchange({0, 0, JW_CMD_IDENTIFY, JW_CHANNEL_NONE, nullptr, 0}));
  jw_identify_response_t after{};
  if (jw_decode_identify_payload(check.data(), check.size(), &after) != JW_RESULT_OK ||
      after.boot_id != identity.boot_id ||
      after.discovery_generation != identity.discovery_generation)
    return absl::FailedPreconditionError("Discovery changed during metadata query.");
  ABSL_ASSIGN_OR_RETURN(auto state_bytes,
                        exchange({0, 0, JW_CMD_GET_STATUS, JW_CHANNEL_NONE, nullptr, 0}));
  jw_device_status_t state{};
  if (jw_decode_device_status(state_bytes.data(), state_bytes.size(), &state) != JW_RESULT_OK)
    return absl::DataLossError("Malformed board state.");
  if (state.recovery & JW_RECOVERY_CLEAR_ESTOP) {
    if (!options.allow_recover)
      return absl::FailedPreconditionError("ESTOP is latched; explicit --allow_recover required.");
    const uint8_t domain[2] = {};
    ABSL_RETURN_IF_ERROR(exchange({0, 0, JW_CMD_RECOVER, JW_CHANNEL_NONE, domain, 2}).status());
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
      status_command(JW_ACTUATOR_CONFIGURE, channel, config_payload, sizeof(config_payload)));
  output << "CONFIGURE_CHANNEL OK; no enable sent\n";

  auto feedback = [&]() -> absl::StatusOr<jw_feedback_t> {
    uint8_t bytes[JW_PROFILE_HEADER_LEN];
    jw_profile_request_t request{JW_PROFILE_ACTUATOR, 1, 0, JW_ACTUATOR_GET_FEEDBACK, nullptr, 0};
    jw_encode_profile_request(bytes, sizeof(bytes), &request);
    ABSL_ASSIGN_OR_RETURN(auto reply,
                          exchange({0, 0, JW_CMD_PROFILE, channel, bytes, sizeof(bytes)}));
    jw_feedback_t value{};
    if (jw_decode_feedback_payload(reply.data(), reply.size(), &value) != JW_RESULT_OK ||
        value.metadata.boot_id != identity.boot_id || value.metadata.validity != JW_SAMPLE_VALID ||
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
    return status_command(JW_ACTUATOR_SET_TARGET, channel, payload, sizeof(payload));
  };
  ABSL_RETURN_IF_ERROR(target(initial.position));
  ABSL_RETURN_IF_ERROR(status_command(JW_ACTUATOR_ENABLE, channel));
  output << "ENABLE OK\n";
  ABSL_RETURN_IF_ERROR(target(*options.target_steps));
  output << "SET_TARGET accepted; this does not prove physical motion or arrival\n";
  ABSL_ASSIGN_OR_RETURN(auto final_feedback, feedback());
  (void)final_feedback;
  ABSL_RETURN_IF_ERROR(status_command(JW_ACTUATOR_DISABLE, channel));
  output << "DISABLE OK\n";
  return absl::OkStatus();
}
}  // namespace

absl::Status ValidateSerialOptions(const Board& board, const SerialValidationOptions& options) {
  if (!SupportedBoard(board.board_type()) || board.name().empty() ||
      board.protocol() != JOSHUA_WIRE || board.firmware().min_proto_version() > JW_PROTO_VERSION ||
      board.comm().comm_type() != robot::comm::SERIAL ||
      board.comm().transport_type() != robot::comm::MESSAGE || !board.comm().has_serial_config() ||
      board.comm().serial_config().message_adapter() != robot::comm::JOSHUA_WIRE ||
      board.comm().serial_config().port().empty() || board.comm().serial_config().baudrate() == 0 ||
      board.comm().serial_config().baudrate() > std::numeric_limits<int>::max()) {
    return absl::InvalidArgumentError(
        "Select a named AM243/TEENSY41/ESP32 board with explicit JOSHUA_WIRE and SERIAL/MESSAGE "
        "config.");
  }
  ABSL_RETURN_IF_ERROR(robot::comm::ValidateSerialCommConfig(board.comm()));
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
  if (options.channel < 0 || options.channel >= JW_MAX_FUNCTIONS) {
    return absl::InvalidArgumentError("Select a channel function ID in 0..254.");
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

absl::Status RunSerialValidation(const Board& board,
                                 const SerialValidationOptions& options,
                                 std::shared_ptr<robot::comm::MessageTransport> transport,
                                 std::ostream& output,
                                 std::function<bool()> cancelled) {
  ABSL_RETURN_IF_ERROR(ValidateSerialOptions(board, options));
  if (!transport) return absl::InvalidArgumentError("Missing message transport.");
  JoshuaWireSession session(std::move(transport));
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
