// Implements v2 session lifecycle, serialization, correlation and ID rotation.
// Only neutral command payloads cross the public API; only v2 frames reach comm.
#include "robot/board/joshua_wire/joshua_wire_session.h"

#include <random>
#include <utility>

#include "firmware/common/joshua_wire.h"
#include "utils/status_macros.h"

namespace robot::board {
namespace {
absl::Status CheckOk(const std::vector<uint8_t>& payload) {
  if (payload.size() != 1 || payload[0] != JW_STATUS_OK) {
    return absl::FailedPreconditionError("JoshuaWire session management was rejected.");
  }
  return absl::OkStatus();
}
}  // namespace

JoshuaWireSession::JoshuaWireSession(
    std::shared_ptr<robot::comm::MessageTransport> transport,
    SessionIdSource source,
    uint32_t message_id_limit,
    std::shared_ptr<robot::comm::CorrelatedCyclicTransport> cyclic,
    absl::Duration cyclic_timeout)
    : transport_(std::move(transport)),
      cyclic_(std::move(cyclic)),
      cyclic_timeout_(cyclic_timeout),
      source_(source ? std::move(source) : [] { return std::random_device{}(); }),
      message_id_limit_(message_id_limit) {}

absl::StatusOr<std::vector<uint8_t>> JoshuaWireSession::ExchangeLocked(uint8_t cmd,
                                                                         uint8_t channel,
                                                                         const uint8_t* payload,
                                                                         uint8_t payload_len) {
  uint8_t bytes[JW_MAX_FRAME_LEN];
  const int len = jw_encode_frame(
      bytes, sizeof(bytes), session_id_, next_message_id_, cmd, channel, payload, payload_len);
  if (len < 0) return absl::InvalidArgumentError("Invalid JoshuaWire request.");
  // Consume IDs even on failure. UINT32_MAX is reserved for the final ESTOP;
  // it is never incremented or reused in an active session.
  if (next_message_id_ < message_id_limit_)
    ++next_message_id_;
  else
    ready_ = false;
  const std::vector<uint8_t> request(bytes, bytes + len);
  // One allocator/lock spans both planes. Message-only transports retain all
  // commands; paired transports route only target/feedback through cyclic I/O.
  auto result = cyclic_ && (cmd == JW_CMD_SET_TARGET || cmd == JW_CMD_GET_FEEDBACK)
                    ? cyclic_->Exchange(request, cyclic_timeout_)
                    : transport_->Exchange(request);
  if (!result.ok()) {
    return absl::Status(result.status().code(),
                        std::string(result.status().message()) + "; command outcome unknown");
  }
  jw_frame_t sent;
  jw_frame_t received;
  if (jw_decode_frame(request.data(), request.size(), &sent) != 0 ||
      jw_decode_frame(result->data(), result->size(), &received) != 0 ||
      !jw_response_matches(&sent, &received)) {
    return absl::DataLossError("Uncorrelated JoshuaWire response; command outcome unknown.");
  }
  return std::vector<uint8_t>(received.payload, received.payload + received.payload_len);
}

absl::Status JoshuaWireSession::ResetLocked() {
  ready_ = false;
  uint32_t candidate = 0;
  for (int attempt = 0; attempt < 16; ++attempt) {
    candidate = source_();
    if (candidate != 0 && candidate != session_id_) break;
  }
  if (candidate == 0 || candidate == session_id_) {
    return absl::UnavailableError("Cannot allocate a new nonzero JoshuaWire session ID.");
  }
  session_id_ = candidate;
  next_message_id_ = 1;
  ABSL_ASSIGN_OR_RETURN(auto response,
                        ExchangeLocked(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE, nullptr, 0));
  ABSL_RETURN_IF_ERROR(CheckOk(response));
  ready_ = true;
  return absl::OkStatus();
}

absl::Status JoshuaWireSession::Open() {
  std::lock_guard<std::mutex> lock(mutex_);
  ready_ = false;
  channel_configs_.clear();
  if (transport_ == nullptr || message_id_limit_ < 3 ||
      (cyclic_ &&
       (cyclic_timeout_ <= absl::ZeroDuration() || cyclic_timeout_ == absl::InfiniteDuration()))) {
    return absl::InvalidArgumentError("Invalid JoshuaWire session configuration.");
  }
  // CommFactory provides an open link; Open here starts a protocol session,
  // not a second physical connection or transport lifecycle.
  return ResetLocked();
}

absl::Status JoshuaWireSession::RotateLocked() {
  ready_ = false;
  // The last ID is never used for a normal command. Disable before resetting,
  // then restore configuration only; an explicit Enable is needed afterward.
  ABSL_ASSIGN_OR_RETURN(auto stopped, ExchangeLocked(JW_CMD_ESTOP, JW_CHANNEL_NONE, nullptr, 0));
  ABSL_RETURN_IF_ERROR(CheckOk(stopped));
  ABSL_RETURN_IF_ERROR(ResetLocked());
  ready_ = false;
  for (const auto& [channel, payload] : channel_configs_) {
    if (next_message_id_ >= message_id_limit_) {
      return absl::ResourceExhaustedError("Insufficient message IDs to restore channels.");
    }
    ABSL_ASSIGN_OR_RETURN(auto response,
                          ExchangeLocked(JW_CMD_CONFIGURE_CHANNEL,
                                         channel,
                                         payload.data(),
                                         static_cast<uint8_t>(payload.size())));
    ABSL_RETURN_IF_ERROR(CheckOk(response));
  }
  ready_ = true;
  return absl::OkStatus();
}

absl::Status JoshuaWireSession::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ready_) return absl::OkStatus();
  ready_ = false;
  auto response = ExchangeLocked(JW_CMD_ESTOP, JW_CHANNEL_NONE, nullptr, 0);
  channel_configs_.clear();
  if (!response.ok()) return response.status();
  return CheckOk(*response);
}

absl::StatusOr<std::vector<uint8_t>> JoshuaWireSession::Exchange(const jw_command_t& command) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ready_) return absl::FailedPreconditionError("JoshuaWire requires a successful reset.");
  if (command.payload_len > JW_MAX_PAYLOAD_LEN ||
      (command.payload_len != 0 && command.payload == nullptr) ||
      command.cmd == JW_CMD_RESET_SESSION) {
    return absl::InvalidArgumentError("Invalid command supplied to v2 session.");
  }
  if (next_message_id_ >= message_id_limit_) {
    ABSL_RETURN_IF_ERROR(RotateLocked());
    return absl::FailedPreconditionError(
        "JoshuaWire session rotated; channels disabled. Enable again.");
  }
  ABSL_ASSIGN_OR_RETURN(
      auto payload,
      ExchangeLocked(command.cmd, command.channel, command.payload, command.payload_len));
  if (command.cmd == JW_CMD_CONFIGURE_CHANNEL && CheckOk(payload).ok()) {
    auto& saved = channel_configs_[command.channel];
    saved.clear();
    if (command.payload_len != 0)
      saved.assign(command.payload, command.payload + command.payload_len);
  }
  return payload;
}

}  // namespace robot::board
