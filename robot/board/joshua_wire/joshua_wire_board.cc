#include "robot/board/joshua_wire/joshua_wire_board.h"

#include <algorithm>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "robot/board/joshua_wire/joshua_wire_session.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/comm/interfaces/message_transport.h"
#include "robot/comm/proto/comm.pb.h"
#include "utils/status_macros.h"

namespace robot::board {

// Board-local correlated session. Channels operate on neutral commands;
// comm remains byte-oriented.
class JoshuaWireCommandClient {
 public:
  JoshuaWireCommandClient(std::shared_ptr<robot::comm::MessageTransport> transport,
                          const robot::comm::PairedTransports* pair)
      : transport_(std::move(transport)) {
    session_ =
        std::make_unique<JoshuaWireSession>(transport_,
                                            JoshuaWireSession::SessionIdSource{},
                                            UINT32_MAX,
                                            pair ? pair->cyclic : nullptr,
                                            pair ? pair->response_timeout : absl::ZeroDuration());
  }
  ~JoshuaWireCommandClient() {
    Close().IgnoreError();
  }
  absl::Status Open() {
    std::lock_guard lock(mutex_);
    return session_->Open();
  }
  absl::Status Close() {
    std::lock_guard lock(mutex_);
    if (closed_) return absl::OkStatus();
    closed_ = true;
    auto status = session_ ? session_->Close() : absl::OkStatus();
    session_.reset();
    transport_.reset();  // Retained channels no longer keep a NIC lease alive.
    return status;
  }
  uint32_t protocol_version() const {
    return JW_PROTO_VERSION;
  }
  absl::StatusOr<std::vector<uint8_t>> Exchange(const jw_command_t& command) {
    std::lock_guard lock(mutex_);
    if (closed_) return absl::FailedPreconditionError("JoshuaWire board has been closed.");
    return session_->Exchange(command);
  }

 private:
  std::shared_ptr<robot::comm::MessageTransport> transport_;
  std::unique_ptr<JoshuaWireSession> session_;
  bool closed_ = false;
  std::mutex mutex_;
};

namespace {
jw_command_t Command(uint8_t cmd,
                     uint8_t function,
                     const uint8_t* data = nullptr,
                     size_t size = 0) {
  return {0, 0, cmd, function, data, size};
}
absl::StatusOr<std::vector<uint8_t>> Data(JoshuaWireCommandClient& client,
                                          const jw_command_t& command) {
  ABSL_ASSIGN_OR_RETURN(auto bytes, client.Exchange(command));
  jw_reply_t reply{};
  if (jw_decode_reply_payload(bytes.data(), bytes.size(), &reply) != JW_RESULT_OK)
    return absl::DataLossError("Malformed common reply.");
  switch (reply.status) {
    case JW_STATUS_OK:
      break;
    case JW_STATUS_UNSUPPORTED:
      return absl::UnimplementedError("Unsupported profile operation.");
    case JW_STATUS_BUSY:
      return absl::ResourceExhaustedError("Firmware queue busy.");
    case JW_STATUS_NOT_READY:
      return absl::UnavailableError("Acquisition not ready.");
    case JW_STATUS_INVALID_PAYLOAD:
      return absl::InvalidArgumentError("Invalid profile payload.");
    case JW_STATUS_OUTCOME_UNKNOWN:
      return absl::UnknownError("Firmware outcome unknown; reconcile state.");
    default:
      return absl::FailedPreconditionError("Firmware rejected operation; query status.");
  }
  if (reply.execution != JW_EXECUTION_COMPLETE)
    return absl::UnavailableError("Action accepted; query its completion before proceeding.");
  return std::vector<uint8_t>(reply.data, reply.data + reply.data_len);
}
absl::StatusOr<std::vector<uint8_t>> Actuator(JoshuaWireCommandClient& client,
                                              uint8_t function,
                                              uint8_t operation,
                                              const uint8_t* data = nullptr,
                                              size_t size = 0) {
  uint8_t payload[JW_MAX_PAYLOAD_LEN];
  const jw_profile_request_t request{JW_PROFILE_ACTUATOR,
                                     JW_PROFILE_VERSION_MAJOR,
                                     JW_PROFILE_VERSION_MINOR,
                                     operation,
                                     data,
                                     size};
  const int length = jw_encode_profile_request(payload, sizeof(payload), &request);
  if (length < 0) return absl::InvalidArgumentError("Invalid actuator request.");
  return Data(client, Command(JW_CMD_PROFILE, function, payload, length));
}
class JoshuaWireChannel : public BoardChannel {
 public:
  JoshuaWireChannel(std::shared_ptr<JoshuaWireCommandClient> client,
                    uint8_t function,
                    uint32_t boot,
                    jw_actuator_schema_t schema)
      : client_(std::move(client)), function_(function), boot_(boot), schema_(schema) {}
  absl::Status Enable() override {
    return Actuator(*client_, function_, JW_ACTUATOR_ENABLE).status();
  }
  absl::Status Disable() override {
    return Actuator(*client_, function_, JW_ACTUATOR_DISABLE).status();
  }
  absl::Status SetTarget(TargetMode mode, float value) override {
    jw_mode_t wire_mode;
    switch (mode) {
      case TargetMode::kPosition:
        wire_mode = JW_MODE_POSITION;
        break;
      case TargetMode::kVelocity:
        wire_mode = JW_MODE_VELOCITY;
        break;
      case TargetMode::kTorque:
        wire_mode = JW_MODE_TORQUE;
        break;
      default:
        return absl::UnimplementedError("Unknown target mode.");
    }
    if (!(schema_.target_modes & (1u << wire_mode)))
      return absl::UnimplementedError("Unadvertised target mode.");
    uint8_t bytes[JW_SET_TARGET_PAYLOAD_LEN];
    const int size = jw_encode_set_target_payload(bytes, sizeof(bytes), wire_mode, value);
    if (size < 0) return absl::InvalidArgumentError("Invalid target value.");
    return Actuator(*client_, function_, JW_ACTUATOR_SET_TARGET, bytes, size).status();
  }
  absl::StatusOr<ChannelFeedback> ReadFeedback() override {
    std::lock_guard lock(feedback_mutex_);
    ABSL_ASSIGN_OR_RETURN(auto data, Actuator(*client_, function_, JW_ACTUATOR_GET_FEEDBACK));
    jw_feedback_t value{};
    if (jw_decode_feedback_payload(data.data(), data.size(), &value) != JW_RESULT_OK)
      return absl::DataLossError("Malformed actuator feedback.");
    if (value.metadata.validity != JW_SAMPLE_VALID || value.metadata.boot_id != boot_ ||
        value.metadata.clock != schema_.feedback_clock)
      return absl::UnavailableError("Feedback invalid, stale, or from another boot/clock.");
    ABSL_ASSIGN_OR_RETURN(const double now, joshua::TryRobotTime());
    if (sample_seen_ && value.metadata.sequence == sequence_) {
      if (value.metadata.timestamp_us != timestamp_ || now < received_ ||
          now - received_ > schema_.max_feedback_age_us / 1'000'000.0)
        return absl::UnavailableError("Cached feedback expired or changed without acquisition.");
    } else {
      if (sample_seen_ && (int32_t(value.metadata.sequence - sequence_) <= 0 ||
                           value.metadata.timestamp_us < timestamp_))
        return absl::DataLossError("Feedback acquisition regressed.");
      received_ = now;
      sequence_ = value.metadata.sequence;
      timestamp_ = value.metadata.timestamp_us;
      sample_seen_ = true;
    }
    ChannelFeedback result;
    result.position = value.position;
    result.velocity = value.velocity;
    result.fault_flags = value.fault_flags;
    return result;
  }

 private:
  std::shared_ptr<JoshuaWireCommandClient> client_;
  uint8_t function_;
  uint32_t boot_;
  jw_actuator_schema_t schema_;
  std::mutex feedback_mutex_;
  bool sample_seen_ = false;
  uint32_t sequence_ = 0;
  uint64_t timestamp_ = 0;
  double received_ = 0.0;
};
absl::Status ConfigureChannel(JoshuaWireCommandClient& commands, const Channel& channel) {
  jw_configure_step_dir_t config{};
  config.max_pulse_rate_hz = channel.step_dir().max_pulse_rate_hz();
  config.invert_dir = channel.step_dir().invert_dir();
  config.enable_active_low = channel.step_dir().enable_active_low();
  config.step_pin = channel.step_dir().step_pin();
  config.dir_pin = channel.step_dir().dir_pin();
  config.enable_pin = channel.step_dir().enable_pin();
  config.step_pulse_width_us = channel.step_dir().step_pulse_width_us();
  uint8_t payload[JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN];
  const int len = jw_encode_configure_step_dir_payload(payload, sizeof(payload), &config);
  if (len < 0) return absl::InvalidArgumentError("Invalid STEP/DIR configuration.");
  return Actuator(commands, channel.index(), JW_ACTUATOR_CONFIGURE, payload, len).status();
}
}  // namespace

absl::Status JoshuaWireBoard::ValidateConfig(const robot::board::Board& config) const {
  if (config.protocol() != BOARD_PROTOCOL_UNSPECIFIED && config.protocol() != JOSHUA_WIRE) {
    return absl::InvalidArgumentError("Unsupported JoshuaWire board protocol.");
  }
  const std::string type_name = robot::board::BoardType_Name(expected_board_type_);
  if (config.board_type() != expected_board_type_) {
    return absl::InvalidArgumentError(
        absl::StrCat("Board '", config.name(), "' is not a ", type_name, " board."));
  }
  if (config.comm().transport_type() != robot::comm::MESSAGE &&
      config.comm().transport_type() != robot::comm::MESSAGE_AND_CYCLIC) {
    return absl::InvalidArgumentError(absl::StrCat(
        "Board '", config.name(), "' requires MESSAGE or MESSAGE_AND_CYCLIC transport."));
  }
  if (config.comm().comm_type() == robot::comm::SERIAL &&
      config.comm().serial_config().message_adapter() != robot::comm::JOSHUA_WIRE) {
    return absl::InvalidArgumentError(
        absl::StrCat("Board '", config.name(), "' requires JOSHUA_WIRE serial messages."));
  }
  if (config.comm().transport_type() == robot::comm::MESSAGE_AND_CYCLIC &&
      config.has_am243_config())
    return absl::InvalidArgumentError(
        "JoshuaWire endpoint fields belong in comm, not "
        "am243_config.");
  if (!config.has_firmware()) {
    return absl::InvalidArgumentError(absl::StrCat(
        type_name, " board '", config.name(), "' requires a firmware{} spec for IDENTIFY."));
  }
  if (config.channels_size() == 0) {
    return absl::InvalidArgumentError(
        absl::StrCat(type_name, " board '", config.name(), "' declares no channels."));
  }
  if (config.channels_size() > JW_MAX_FUNCTIONS) {
    return absl::InvalidArgumentError(absl::StrCat(type_name,
                                                   " board '",
                                                   config.name(),
                                                   "' declares more channels than JoshuaWire "
                                                   "supports (",
                                                   JW_MAX_FUNCTIONS,
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
    if (channel.index() >= JW_MAX_FUNCTIONS) {
      return absl::InvalidArgumentError(absl::StrCat(type_name,
                                                     " board '",
                                                     config.name(),
                                                     "' channel index ",
                                                     channel.index(),
                                                     " exceeds JoshuaWire's max channel "
                                                     "index (",
                                                     JW_MAX_FUNCTIONS - 1,
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

absl::Status JoshuaWireBoard::IdentifyAndValidate(JoshuaWireCommandClient& commands,
                                                  const Board& config) {
  ABSL_ASSIGN_OR_RETURN(auto data, Data(commands, Command(JW_CMD_IDENTIFY, JW_CHANNEL_NONE)));
  jw_identify_response_t identity{};
  if (jw_decode_identify_payload(data.data(), data.size(), &identity) != JW_RESULT_OK ||
      identity.protocol_min > JW_PROTO_VERSION || identity.protocol_max < JW_PROTO_VERSION ||
      JW_PROTO_VERSION < config.firmware().min_proto_version() ||
      !(identity.supported_planes & JW_PLANE_MESSAGE) ||
      (config.comm().transport_type() == robot::comm::MESSAGE_AND_CYCLIC &&
       !(identity.supported_planes & JW_PLANE_CYCLIC)) ||
      !(identity.safety_flags & JW_SAFETY_BOARD_ESTOP) ||
      (identity.safety_flags & ~(JW_SAFETY_BOARD_ESTOP | JW_SAFETY_DOMAIN_ESTOP |
                                 JW_SAFETY_PHYSICAL_VERIFICATION | JW_SAFETY_COMM_INTERRUPTED)))
    return absl::FailedPreconditionError("Incompatible JoshuaWire identity/limits/planes/safety.");
  // The configured transport timeout bounds the acceptable synchronous dispatch.
  const uint64_t timeout_us =
      config.comm().has_serial_config()
          ? uint64_t(config.comm().serial_config().has_exchange_timeout_ms()
                         ? config.comm().serial_config().exchange_timeout_ms()
                         : 100) *
                1000
          : config.comm().ethercat_config().timing().response_timeout_us();
  if (timeout_us && identity.max_handler_us > timeout_us)
    return absl::FailedPreconditionError("Firmware handler exceeds response budget.");
  functions_.clear();
  uint16_t cursor = 0;
  uint32_t generation = 0;
  do {
    uint8_t request[6];
    for (int i = 0; i < 4; ++i) request[i] = generation >> (8 * i);
    request[4] = cursor;
    request[5] = cursor >> 8;
    ABSL_ASSIGN_OR_RETURN(auto bytes,
                          Data(commands, Command(JW_CMD_DISCOVER, JW_CHANNEL_NONE, request, 6)));
    jw_discovery_page_t page{};
    if (jw_decode_discovery_page(bytes.data(), bytes.size(), &page) != JW_RESULT_OK ||
        page.generation != identity.discovery_generation || page.total != identity.function_count ||
        page.count > identity.max_page_entries ||
        (page.next_cursor != JW_CURSOR_END &&
         (page.count == 0 || page.next_cursor != cursor + page.count)))
      return absl::DataLossError("Inconsistent discovery page.");
    for (int i = 0; i < page.count; ++i) {
      const auto& function = page.functions[i];
      if ((!functions_.empty() && function.function_id <= functions_.rbegin()->first) ||
          !functions_.emplace(function.function_id, function).second)
        return absl::DataLossError("Repeated or unsorted function ID.");
    }
    generation = page.generation;
    cursor = page.next_cursor;
  } while (cursor != JW_CURSOR_END && functions_.size() <= JW_MAX_FUNCTIONS);
  if (functions_.size() != identity.function_count)
    return absl::DataLossError("Incomplete function catalog.");
  cursor = 0;
  for (int pages = 0;; ++pages) {
    if (pages >= 4096) return absl::DataLossError("Unbounded dependency catalog.");
    uint8_t request[6];
    for (int i = 0; i < 4; ++i) request[i] = generation >> (8 * i);
    request[4] = cursor;
    request[5] = cursor >> 8;
    ABSL_ASSIGN_OR_RETURN(
        auto bytes, Data(commands, Command(JW_CMD_GET_DEPENDENCIES, JW_CHANNEL_NONE, request, 6)));
    jw_dependency_page_t page{};
    if (jw_decode_dependency_page(bytes.data(), bytes.size(), &page) != JW_RESULT_OK ||
        page.generation != generation ||
        (page.next_cursor != JW_CURSOR_END &&
         (!page.count || page.next_cursor != cursor + page.count)))
      return absl::DataLossError("Inconsistent safety dependency page.");
    for (int i = 0; i < page.count; ++i)
      if (!functions_.count(page.dependencies[i].function_id))
        return absl::DataLossError("Safety dependency refers to unknown function.");
    if (page.next_cursor == JW_CURSOR_END) break;
    cursor = page.next_cursor;
  }
  schemas_.clear();
  for (const auto& channel : config.channels()) {
    auto found = functions_.find(channel.index());
    const uint32_t required = (1u << JW_ACTUATOR_CONFIGURE) | (1u << JW_ACTUATOR_SET_TARGET) |
                              (1u << JW_ACTUATOR_GET_FEEDBACK) | (1u << JW_ACTUATOR_ENABLE) |
                              (1u << JW_ACTUATOR_DISABLE);
    if (found == functions_.end() || found->second.profile_id != JW_PROFILE_ACTUATOR ||
        found->second.profile_major != JW_PROFILE_VERSION_MAJOR ||
        (found->second.operations & required) != required)
      return absl::FailedPreconditionError("Required actuator profile/function unavailable.");
    ABSL_ASSIGN_OR_RETURN(auto bytes,
                          Data(commands, Command(JW_CMD_DESCRIBE_FUNCTION, channel.index())));
    uint8_t expected[JW_FUNCTION_DESCRIPTOR_LEN];
    jw_encode_function_descriptor(expected, sizeof(expected), &found->second);
    if (bytes.size() < JW_FUNCTION_DESCRIPTOR_LEN ||
        !std::equal(expected, expected + sizeof(expected), bytes.begin()))
      return absl::DataLossError("Function changed during metadata query.");
    jw_actuator_schema_t schema{};
    if (jw_decode_actuator_schema(bytes.data() + JW_FUNCTION_DESCRIPTOR_LEN,
                                  bytes.size() - JW_FUNCTION_DESCRIPTOR_LEN,
                                  &schema) != JW_RESULT_OK ||
        schema.drive != JW_DRIVE_STEP_DIR || schema.feedback_clock != JW_CLOCK_BOOT_MONOTONIC_US ||
        schema.position_unit != JW_UNIT_NATIVE || schema.velocity_unit != JW_UNIT_NATIVE)
      return absl::FailedPreconditionError("Unsupported actuator schema/units/clock.");
    schemas_[channel.index()] = schema;
  }
  ABSL_ASSIGN_OR_RETURN(auto after, Data(commands, Command(JW_CMD_IDENTIFY, JW_CHANNEL_NONE)));
  jw_identify_response_t current{};
  if (jw_decode_identify_payload(after.data(), after.size(), &current) != JW_RESULT_OK ||
      current.boot_id != identity.boot_id || current.discovery_generation != generation)
    return absl::FailedPreconditionError("Board rebooted or discovery changed.");
  for (const auto& [id, function] : functions_) {
    ABSL_ASSIGN_OR_RETURN(auto bytes, Data(commands, Command(JW_CMD_GET_STATUS, id)));
    jw_device_status_t state{};
    if (jw_decode_device_status(bytes.data(), bytes.size(), &state) != JW_RESULT_OK ||
        state.stop_phase == JW_STOP_ACCEPTED || state.action_execution == JW_EXECUTION_ACCEPTED)
      return absl::FailedPreconditionError("Function state has not reconciled.");
  }
  ABSL_ASSIGN_OR_RETURN(auto board_state,
                        Data(commands, Command(JW_CMD_GET_STATUS, JW_CHANNEL_NONE)));
  jw_device_status_t state{};
  if (jw_decode_device_status(board_state.data(), board_state.size(), &state) != JW_RESULT_OK ||
      (state.recovery & JW_RECOVERY_CLEAR_ESTOP))
    return absl::FailedPreconditionError("Board requires explicit recovery.");
  boot_id_ = identity.boot_id;
  return absl::OkStatus();
}

absl::Status JoshuaWireBoard::Init(const robot::board::Board& config) {
  if (initialized_) {
    return absl::FailedPreconditionError(
        absl::StrCat("Board '", config.name(), "' is already initialized."));
  }
  ABSL_RETURN_IF_ERROR(ValidateConfig(config));

  ABSL_ASSIGN_OR_RETURN(auto transports, robot::comm::CommFactory::CreateComm(config.comm()));
  ABSL_ASSIGN_OR_RETURN(auto transport,
                        robot::comm::GetCommTransport<robot::comm::MessageTransport>(transports));
  const auto* pair = std::get_if<robot::comm::PairedTransports>(&transports);
  const bool wants_pair = config.comm().transport_type() == robot::comm::MESSAGE_AND_CYCLIC;
  if (wants_pair != (pair != nullptr) ||
      (pair && (!pair->cyclic || pair->response_timeout <= absl::ZeroDuration() ||
                pair->response_timeout == absl::InfiniteDuration())))
    return absl::InvalidArgumentError(
        "Factory capabilities do not match JoshuaWire routing policy.");

  auto commands = std::make_shared<JoshuaWireCommandClient>(std::move(transport), pair);
  ABSL_RETURN_IF_ERROR(commands->Open());
  ABSL_RETURN_IF_ERROR(IdentifyAndValidate(*commands, config));

  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels;
  for (const auto& channel_config : config.channels()) {
    ABSL_RETURN_IF_ERROR(ConfigureChannel(*commands, channel_config));
    channels[channel_config.index()] =
        std::make_shared<JoshuaWireChannel>(commands,
                                            static_cast<uint8_t>(channel_config.index()),
                                            boot_id_,
                                            schemas_.at(channel_config.index()));
  }

  name_ = config.name();
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
    return absl::NotFoundError(absl::StrCat(
        "Board '", name_, "' has no channel ", index, "; declare it in the board's channels{}."));
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
