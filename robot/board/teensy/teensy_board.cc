#include "robot/board/teensy/teensy_board.h"

#include <set>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "firmware/common/joshua_wire_v1.h"
#include "robot/board/frame/serial_frame_transport.h"
#include "robot/comm/factory/comm_factory.h"
#include "utils/status_macros.h"

namespace robot::board {

namespace {

std::function<absl::StatusOr<std::shared_ptr<FrameTransport>>(const robot::comm::Comm&)>&
FrameTransportFactoryForTesting() {
  static std::function<absl::StatusOr<std::shared_ptr<FrameTransport>>(const robot::comm::Comm&)>
      factory;
  return factory;
}

absl::Status JwStatusToAbsl(jw1_status_t status, const std::string& what) {
  switch (status) {
    case JW1_STATUS_OK:
      return absl::OkStatus();
    case JW1_STATUS_UNSUPPORTED:
      return absl::UnimplementedError(absl::StrCat(what, ": firmware reports unsupported."));
    case JW1_STATUS_ERROR:
    default:
      return absl::InternalError(absl::StrCat(what, ": firmware reports error."));
  }
}

// One STEP_DIR channel over a joshua_wire_v1 FrameTransport. The wire
// protocol carries native units only (steps, steps/sec); unit conversion
// (degrees<->steps) lives in the motor driver (StepperDriver), matching
// every other board (docs/BOARD_LAYER_RFC.md §5.3).
class TeensyChannel : public BoardChannel {
 public:
  TeensyChannel(std::shared_ptr<FrameTransport> transport, uint8_t channel_index)
      : transport_(std::move(transport)), channel_index_(channel_index) {}

  absl::Status Enable() override {
    uint8_t buf[JW1_MAX_FRAME_LEN];
    const int len = jw1_encode_enable(buf, sizeof(buf), channel_index_);
    return SendExpectStatus(buf, len, "Enable");
  }

  absl::Status Disable() override {
    uint8_t buf[JW1_MAX_FRAME_LEN];
    const int len = jw1_encode_disable(buf, sizeof(buf), channel_index_);
    return SendExpectStatus(buf, len, "Disable");
  }

  absl::Status SetTarget(TargetMode mode, float value) override {
    if (mode == TargetMode::kTorque) {
      // STEP_DIR is an open-loop drive interface with no torque target
      // (docs/BOARD_LAYER_RFC.md §12.7 — a board that cannot do a mode
      // returns UnimplementedError from it, mirrors FeetechBusChannel).
      return absl::UnimplementedError("TEENSY41 STEP_DIR channel has no torque target.");
    }
    const jw1_mode_t wire_mode =
        mode == TargetMode::kPosition ? JW1_MODE_POSITION : JW1_MODE_VELOCITY;
    uint8_t buf[JW1_MAX_FRAME_LEN];
    const int len = jw1_encode_set_target(buf, sizeof(buf), channel_index_, wire_mode, value);
    return SendExpectStatus(buf, len, "SetTarget");
  }

  absl::StatusOr<ChannelFeedback> ReadFeedback() override {
    uint8_t buf[JW1_MAX_FRAME_LEN];
    const int len = jw1_encode_get_feedback_request(buf, sizeof(buf), channel_index_);
    if (len < 0) {
      return absl::InternalError("Failed to encode GET_FEEDBACK request.");
    }
    ABSL_ASSIGN_OR_RETURN(
        auto response,
        transport_->SendAndReceive(std::vector<uint8_t>(buf, buf + len),
                                   JW1_FRAME_LEN(JW1_FEEDBACK_RESPONSE_PAYLOAD_LEN)));
    jw1_frame_t frame;
    if (jw1_decode_frame(response.data(), response.size(), &frame) != 0) {
      return absl::InternalError("Malformed GET_FEEDBACK response frame.");
    }
    jw1_feedback_t feedback;
    if (jw1_decode_feedback_response(&frame, &feedback) != 0) {
      return absl::InternalError("Malformed GET_FEEDBACK response payload.");
    }
    ChannelFeedback out;
    out.position = feedback.position;
    out.velocity = feedback.velocity;
    out.fault_flags = feedback.fault_flags;
    return out;
  }

 private:
  absl::Status SendExpectStatus(const uint8_t* buf, int len, const std::string& what) {
    if (len < 0) {
      return absl::InternalError(absl::StrCat("Failed to encode ", what, " request."));
    }
    ABSL_ASSIGN_OR_RETURN(
        auto response,
        transport_->SendAndReceive(std::vector<uint8_t>(buf, buf + len),
                                   JW1_FRAME_LEN(JW1_STATUS_RESPONSE_PAYLOAD_LEN)));
    jw1_frame_t frame;
    if (jw1_decode_frame(response.data(), response.size(), &frame) != 0) {
      return absl::InternalError(absl::StrCat("Malformed ", what, " response frame."));
    }
    jw1_status_t status;
    if (jw1_decode_status_response(&frame, &status) != 0) {
      return absl::InternalError(absl::StrCat("Malformed ", what, " response payload."));
    }
    return JwStatusToAbsl(status, what);
  }

  std::shared_ptr<FrameTransport> transport_;
  const uint8_t channel_index_;
};

absl::Status ValidateConfig(const robot::board::Board& config) {
  if (config.board_type() != robot::board::BoardType::TEENSY41) {
    return absl::InvalidArgumentError(
        absl::StrCat("Board '", config.name(), "' is not a TEENSY41 board."));
  }
  if (config.comm().comm_type() != robot::comm::CommType::SERIAL ||
      !config.comm().has_serial_config()) {
    return absl::InvalidArgumentError(
        absl::StrCat("TEENSY41 board '", config.name(), "' requires SERIAL comm config."));
  }
  if (!config.has_firmware()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "TEENSY41 board '", config.name(), "' requires a firmware{} spec for IDENTIFY."));
  }
  if (config.channels_size() == 0) {
    return absl::InvalidArgumentError(
        absl::StrCat("TEENSY41 board '", config.name(), "' declares no channels."));
  }
  if (config.channels_size() > JW1_MAX_CHANNELS) {
    return absl::InvalidArgumentError(absl::StrCat("TEENSY41 board '",
                                                   config.name(),
                                                   "' declares more channels than joshua_wire_v1 "
                                                   "supports (",
                                                   JW1_MAX_CHANNELS,
                                                   ")."));
  }
  std::set<uint32_t> seen_indices;
  for (const auto& channel : config.channels()) {
    if (channel.drive() != robot::board::DriveInterface::STEP_DIR) {
      return absl::InvalidArgumentError(absl::StrCat("TEENSY41 board '",
                                                     config.name(),
                                                     "' channel ",
                                                     channel.index(),
                                                     " must use the STEP_DIR drive."));
    }
    for (uint32_t pin : {channel.step_dir().step_pin(),
                         channel.step_dir().dir_pin(),
                         channel.step_dir().enable_pin()}) {
      if (pin > 255) {
        return absl::InvalidArgumentError(
            absl::StrCat("TEENSY41 board '",
                         config.name(),
                         "' channel ",
                         channel.index(),
                         " declares a pin number ",
                         pin,
                         " that doesn't fit an MCU GPIO pin (max 255)."));
      }
    }
    if (channel.index() >= JW1_MAX_CHANNELS) {
      return absl::InvalidArgumentError(absl::StrCat("TEENSY41 board '",
                                                     config.name(),
                                                     "' channel index ",
                                                     channel.index(),
                                                     " exceeds joshua_wire_v1's max channel "
                                                     "index (",
                                                     JW1_MAX_CHANNELS - 1,
                                                     "); it must match the firmware channel "
                                                     "table's array position."));
    }
    if (!seen_indices.insert(channel.index()).second) {
      return absl::InvalidArgumentError(absl::StrCat("TEENSY41 board '",
                                                     config.name(),
                                                     "' declares channel index ",
                                                     channel.index(),
                                                     " more than once."));
    }
  }
  return absl::OkStatus();
}

// IDENTIFY handshake: protocol version and per-channel drive capability
// must agree with config, or Init() fails now instead of the first
// SET_TARGET silently landing on the wrong channel. No firmware-name
// check — a free-form name string isn't a generalizable compatibility
// check (docs/BOARD_LAYER_RFC.md §7.5); these structural facts are.
absl::Status IdentifyAndValidate(FrameTransport& transport, const robot::board::Board& config) {
  uint8_t request[JW1_MAX_FRAME_LEN];
  const int request_len = jw1_encode_identify_request(request, sizeof(request));
  if (request_len < 0) {
    return absl::InternalError("Failed to encode IDENTIFY request.");
  }
  ABSL_ASSIGN_OR_RETURN(
      auto response,
      transport.SendAndReceive(std::vector<uint8_t>(request, request + request_len),
                               JW1_FRAME_LEN(JW1_IDENTIFY_RESPONSE_PAYLOAD_LEN)));

  jw1_frame_t frame;
  if (jw1_decode_frame(response.data(), response.size(), &frame) != 0) {
    return absl::UnavailableError(absl::StrCat(
        "Board '", config.name(), "': malformed IDENTIFY response; check wiring/firmware."));
  }
  if (frame.proto_ver < config.firmware().min_proto_version()) {
    return absl::FailedPreconditionError(absl::StrCat("Board '",
                                                      config.name(),
                                                      "': firmware proto_ver ",
                                                      frame.proto_ver,
                                                      " is older than the configured "
                                                      "min_proto_version ",
                                                      config.firmware().min_proto_version(),
                                                      "."));
  }

  jw1_identify_response_t identify;
  if (jw1_decode_identify_response(&frame, &identify) != 0) {
    return absl::UnavailableError(
        absl::StrCat("Board '", config.name(), "': malformed IDENTIFY payload."));
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
    if (identify.channel_drives[channel.index()] != JW1_DRIVE_STEP_DIR) {
      return absl::FailedPreconditionError(
          absl::StrCat("Board '",
                       config.name(),
                       "': config declares channel ",
                       channel.index(),
                       " as STEP_DIR, but firmware IDENTIFY reports a different drive."));
    }
  }
  return absl::OkStatus();
}

absl::Status ConfigureChannel(FrameTransport& transport, const robot::board::Channel& channel) {
  jw1_configure_step_dir_t config{};
  config.max_pulse_rate_hz = channel.step_dir().max_pulse_rate_hz();
  config.invert_dir = channel.step_dir().invert_dir() ? 1 : 0;
  config.enable_active_low = channel.step_dir().enable_active_low() ? 1 : 0;
  config.step_pin = static_cast<uint8_t>(channel.step_dir().step_pin());
  config.dir_pin = static_cast<uint8_t>(channel.step_dir().dir_pin());
  config.enable_pin = static_cast<uint8_t>(channel.step_dir().enable_pin());

  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_configure_channel_step_dir(
      buf, sizeof(buf), static_cast<uint8_t>(channel.index()), &config);
  if (len < 0) {
    return absl::InternalError("Failed to encode CONFIGURE_CHANNEL request.");
  }
  ABSL_ASSIGN_OR_RETURN(auto response,
                        transport.SendAndReceive(std::vector<uint8_t>(buf, buf + len),
                                                 JW1_FRAME_LEN(JW1_STATUS_RESPONSE_PAYLOAD_LEN)));
  jw1_frame_t frame;
  if (jw1_decode_frame(response.data(), response.size(), &frame) != 0) {
    return absl::InternalError("Malformed CONFIGURE_CHANNEL response frame.");
  }
  jw1_status_t status;
  if (jw1_decode_status_response(&frame, &status) != 0) {
    return absl::InternalError("Malformed CONFIGURE_CHANNEL response payload.");
  }
  return JwStatusToAbsl(status, absl::StrCat("CONFIGURE_CHANNEL(", channel.index(), ")"));
}

}  // namespace

absl::Status TeensyBoard::Init(const robot::board::Board& config) {
  if (initialized_) {
    return absl::FailedPreconditionError(
        absl::StrCat("TEENSY41 board '", config.name(), "' is already initialized."));
  }
  ABSL_RETURN_IF_ERROR(ValidateConfig(config));

  std::shared_ptr<FrameTransport> transport;
  if (FrameTransportFactoryForTesting()) {
    ABSL_ASSIGN_OR_RETURN(transport, FrameTransportFactoryForTesting()(config.comm()));
  } else {
    ABSL_ASSIGN_OR_RETURN(auto serial, robot::comm::CommFactory::CreateSerial(config.comm()));
    transport = std::make_shared<SerialFrameTransport>(std::move(serial));
  }

  ABSL_RETURN_IF_ERROR(IdentifyAndValidate(*transport, config));

  std::map<uint32_t, std::shared_ptr<BoardChannel>> channels;
  for (const auto& channel_config : config.channels()) {
    ABSL_RETURN_IF_ERROR(ConfigureChannel(*transport, channel_config));
    channels[channel_config.index()] =
        std::make_shared<TeensyChannel>(transport, static_cast<uint8_t>(channel_config.index()));
  }

  config_ = config;
  transport_ = std::move(transport);
  channels_ = std::move(channels);
  initialized_ = true;
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<BoardChannel>> TeensyBoard::OpenChannel(uint32_t index) {
  if (!initialized_) {
    return absl::FailedPreconditionError("TEENSY41 board is not initialized.");
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

absl::Status TeensyBoard::Teardown() {
  channels_.clear();
  transport_.reset();
  initialized_ = false;
  return absl::OkStatus();
}

void TeensyBoard::SetFrameTransportFactoryForTesting(
    std::function<absl::StatusOr<std::shared_ptr<FrameTransport>>(const robot::comm::Comm&)>
        factory) {
  FrameTransportFactoryForTesting() = std::move(factory);
}

}  // namespace robot::board
