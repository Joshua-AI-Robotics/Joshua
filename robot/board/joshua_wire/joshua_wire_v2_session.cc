// Implements host-side v2 session lifecycle, request serialization, correlation
// checks and ID-exhaustion recovery. This temporary adapter translates the
// existing engine's in-memory v1 commands into v2 wire frames; the underlying
// MessageTransport owns actual I/O and deadlines, and there is no v1 fallback.
#include "robot/board/joshua_wire/joshua_wire_v2_session.h"

#include <random>
#include <utility>

#include "firmware/common/joshua_wire_v1.h"
#include "firmware/common/joshua_wire_v2.h"
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

JoshuaWireV2Session::JoshuaWireV2Session(std::shared_ptr<robot::comm::MessageTransport> transport,
                                         SessionIdSource source,
                                         uint32_t message_id_limit)
    : transport_(std::move(transport)),
      source_(source ? std::move(source) : [] { return std::random_device{}(); }),
      message_id_limit_(message_id_limit) {}

absl::StatusOr<std::vector<uint8_t>> JoshuaWireV2Session::ExchangeLocked(uint8_t cmd,
                                                                         uint8_t channel,
                                                                         const uint8_t* payload,
                                                                         uint8_t payload_len) {
  uint8_t bytes[JW2_MAX_FRAME_LEN];
  const int len = jw2_encode_frame(
      bytes, sizeof(bytes), session_id_, next_message_id_, cmd, channel, payload, payload_len);
  if (len < 0) return absl::InvalidArgumentError("Invalid JoshuaWire v2 request.");
  // Consume IDs even on failure. UINT32_MAX is reserved for the final ESTOP;
  // it is never incremented or reused in an active session.
  if (next_message_id_ < message_id_limit_)
    ++next_message_id_;
  else
    ready_ = false;
  const std::vector<uint8_t> request(bytes, bytes + len);
  auto result = transport_->Exchange(request);
  if (!result.ok()) {
    return absl::Status(result.status().code(),
                        std::string(result.status().message()) + "; command outcome unknown");
  }
  jw2_frame_t sent;
  jw2_frame_t received;
  if (jw2_decode_frame(request.data(), request.size(), &sent) != 0 ||
      jw2_decode_frame(result->data(), result->size(), &received) != 0 ||
      !jw2_response_matches(&sent, &received)) {
    return absl::DataLossError("Uncorrelated JoshuaWire v2 response; command outcome unknown.");
  }
  return std::vector<uint8_t>(received.payload, received.payload + received.payload_len);
}

absl::Status JoshuaWireV2Session::ResetLocked() {
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

absl::Status JoshuaWireV2Session::Open() {
  std::lock_guard<std::mutex> lock(mutex_);
  ready_ = false;
  channel_configs_.clear();
  if (transport_ == nullptr || message_id_limit_ < 3) {
    return absl::InvalidArgumentError("Invalid JoshuaWire v2 session configuration.");
  }
  ABSL_RETURN_IF_ERROR(transport_->Open());
  return ResetLocked();
}

absl::Status JoshuaWireV2Session::RotateLocked() {
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

absl::Status JoshuaWireV2Session::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ready_) return absl::OkStatus();
  ready_ = false;
  auto response = ExchangeLocked(JW_CMD_ESTOP, JW_CHANNEL_NONE, nullptr, 0);
  channel_configs_.clear();
  if (!response.ok()) return response.status();
  return CheckOk(*response);
}

absl::Status JoshuaWireV2Session::Write(const std::vector<uint8_t>& request) {
  return absl::UnimplementedError("JoshuaWire v2 requires correlated request/response exchange.");
}

absl::StatusOr<std::vector<uint8_t>> JoshuaWireV2Session::SendAndReceive(
    const std::vector<uint8_t>& request, size_t expected_response_size) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ready_) return absl::FailedPreconditionError("JoshuaWire v2 requires a successful reset.");
  jw1_frame_t command;
  if (jw1_decode_frame(request.data(), request.size(), &command) != 0 ||
      command.cmd == JW_CMD_RESET_SESSION) {
    return absl::InvalidArgumentError("Invalid legacy command payload supplied to v2 session.");
  }
  if (next_message_id_ >= message_id_limit_) {
    ABSL_RETURN_IF_ERROR(RotateLocked());
    return absl::FailedPreconditionError(
        "JoshuaWire session rotated; channels disabled. Enable again.");
  }
  ABSL_ASSIGN_OR_RETURN(
      auto payload,
      ExchangeLocked(command.cmd, command.channel, command.payload, command.payload_len));
  if (payload.size() > JW1_MAX_PAYLOAD_LEN) {
    return absl::DataLossError("Response exceeds the legacy command payload limit.");
  }
  if (command.cmd == JW_CMD_CONFIGURE_CHANNEL && CheckOk(payload).ok()) {
    channel_configs_[command.channel] =
        std::vector<uint8_t>(command.payload, command.payload + command.payload_len);
  }
  // Only the semantic payload is reused by the existing engine. This session
  // has already validated v2 correlation and accepted a v2 frame.
  uint8_t response[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_frame(response,
                                   sizeof(response),
                                   command.cmd,
                                   command.channel,
                                   payload.data(),
                                   static_cast<uint8_t>(payload.size()));
  if (len < 0) return absl::DataLossError("Cannot decode JoshuaWire command response.");
  return std::vector<uint8_t>(response, response + len);
}

}  // namespace robot::board
