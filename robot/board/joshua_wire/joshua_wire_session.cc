// Implements JW session lifecycle, serialization, correlation and ID rotation.
// Only neutral command payloads cross the public API; only JW frames reach comm.
#include "robot/board/joshua_wire/joshua_wire_session.h"

#include <random>
#include <utility>

#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "utils/status_macros.h"
#include "utils/scope_exit.h"

namespace robot::board {
namespace {
absl::Status CheckOk(const std::vector<uint8_t>& payload) {
  jw_reply_t reply{};
  if (jw_decode_reply_payload(payload.data(), payload.size(), &reply) != JW_RESULT_OK ||
      reply.status != JW_STATUS_OK || reply.execution != JW_EXECUTION_COMPLETE) {
    return absl::FailedPreconditionError("JoshuaWire management did not complete.");
  }
  return absl::OkStatus();
}
}  // namespace

JoshuaWireSession::JoshuaWireSession(std::shared_ptr<robot::comm::MessageTransport> transport,
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
  jw_profile_request_t profile{};
  const bool on_cyclic =
      cyclic_ && cmd == JW_CMD_PROFILE &&
      jw_decode_profile_request(payload, payload_len, &profile) == JW_RESULT_OK &&
      profile.profile_id == JW_PROFILE_ACTUATOR && profile.major == JW_PROFILE_VERSION_MAJOR &&
      profile.minor == JW_PROFILE_VERSION_MINOR &&
      (profile.operation == JW_ACTUATOR_SET_TARGET ||
       profile.operation == JW_ACTUATOR_GET_FEEDBACK);
  jw_frame_t sent{}, received{};
  jw_decode_frame(request.data(), request.size(), &sent);
  absl::Status failure = absl::UnavailableError("JoshuaWire response missing");
  // Retry identical bytes/IDs only. The shared firmware endpoint replays its
  // last reply without repeating side effects, including unknown outcomes.
  std::vector<uint8_t> response;
  bool completed = false;
  joshua::ScopeExit track_outcome([&] {
    if (!completed) uncertain_ = true;
  });
  for (int attempt = 0; attempt < 2; ++attempt) {
    auto result =
        on_cyclic ? cyclic_->Exchange(request, cyclic_timeout_) : transport_->Exchange(request);
    if (!result.ok()) {
      failure = result.status();
      continue;
    }
    response = std::move(*result);
    if (jw_decode_frame(response.data(), response.size(), &received) != JW_RESULT_OK ||
        !jw_response_matches(&sent, &received)) {
      failure = absl::DataLossError("Uncorrelated JoshuaWire response");
      continue;
    }
    jw_reply_t reply{};
    if (jw_decode_reply_payload(received.payload, received.payload_len, &reply) != JW_RESULT_OK) {
      failure = absl::DataLossError("Malformed JoshuaWire common reply");
      continue;
    }
    if (reply.status == JW_STATUS_OUTCOME_UNKNOWN) uncertain_ = true;
    std::vector<uint8_t> reply_payload(received.payload, received.payload + received.payload_len);
    completed = true;
    return reply_payload;
  }
  uncertain_ = true;
  return absl::Status(failure.code(), std::string(failure.message()) + "; command outcome unknown");
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
  uncertain_ = false;
  pending_.clear();
  ready_ = true;
  return absl::OkStatus();
}

absl::Status JoshuaWireSession::Open() {
  std::lock_guard<std::mutex> lock(mutex_);
  ready_ = false;
  pending_.clear();
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
  const uint8_t domain[2] = {};
  ABSL_ASSIGN_OR_RETURN(auto stopped, ExchangeLocked(JW_CMD_ESTOP, JW_CHANNEL_NONE, domain, 2));
  ABSL_RETURN_IF_ERROR(CheckOk(stopped));
  // Rotation never restores configuration or clears an ESTOP latch. The board
  // must reconcile discovery/state and apply desired config explicitly.
  return absl::OkStatus();
}

absl::Status JoshuaWireSession::Close() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (session_id_ == 0) return absl::OkStatus();
  ready_ = false;
  const uint8_t domain[2] = {};
  joshua::ScopeExit clear_session([&] {
    session_id_ = 0;
    pending_.clear();
  });
  auto response = ExchangeLocked(JW_CMD_ESTOP, JW_CHANNEL_NONE, domain, 2);
  if (!response.ok()) return response.status();
  return CheckOk(*response);
}

absl::StatusOr<std::vector<uint8_t>> JoshuaWireSession::Exchange(const jw_command_t& command) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ready_) return absl::FailedPreconditionError("JoshuaWire requires a successful reset.");
  if (command.payload_len > JW_MAX_PAYLOAD_LEN ||
      (command.payload_len != 0 && command.payload == nullptr) ||
      command.cmd == JW_CMD_RESET_SESSION) {
    return absl::InvalidArgumentError("Invalid command supplied to JW session.");
  }
  if (next_message_id_ >= message_id_limit_) {
    ABSL_RETURN_IF_ERROR(RotateLocked());
    return absl::FailedPreconditionError(
        "JoshuaWire session rotated; channels disabled. Enable again.");
  }
  const bool query = command.cmd == JW_CMD_IDENTIFY || command.cmd == JW_CMD_DISCOVER ||
                     command.cmd == JW_CMD_DESCRIBE_FUNCTION || command.cmd == JW_CMD_GET_STATUS ||
                     command.cmd == JW_CMD_GET_DEPENDENCIES;
  jw_profile_request_t profile{};
  const bool is_profile =
      command.cmd == JW_CMD_PROFILE &&
      jw_decode_profile_request(command.payload, command.payload_len, &profile) == JW_RESULT_OK;
  const bool profile_read =
      is_profile && profile.major == JW_PROFILE_VERSION_MAJOR &&
      profile.minor == JW_PROFILE_VERSION_MINOR &&
      ((profile.profile_id == JW_PROFILE_ACTUATOR &&
        profile.operation == JW_ACTUATOR_GET_FEEDBACK) ||
       (profile.profile_id == JW_PROFILE_SENSOR && profile.operation == JW_SENSOR_READ) ||
       (profile.profile_id == JW_PROFILE_IO && profile.operation == JW_IO_READ));
  if ((uncertain_ || pending_.count(JW_CHANNEL_NONE) || pending_.count(command.channel)) &&
      !query && !profile_read && command.cmd != JW_CMD_ESTOP && command.cmd != JW_CMD_RECOVER)
    return absl::FailedPreconditionError("Reconcile unknown/pending action before mutation.");
  bool recorded = false;
  joshua::ScopeExit record_outcome([&] {
    if (!recorded) uncertain_ = true;
  });
  ABSL_ASSIGN_OR_RETURN(
      auto payload,
      ExchangeLocked(command.cmd, command.channel, command.payload, command.payload_len));
  jw_reply_t reply{};
  jw_decode_reply_payload(payload.data(), payload.size(), &reply);
  if (reply.execution == JW_EXECUTION_ACCEPTED)
    pending_[command.channel] = {session_id_, next_message_id_ - 1};
  if (command.cmd == JW_CMD_GET_STATUS && reply.status == JW_STATUS_OK) {
    jw_device_status_t state{};
    if (jw_decode_device_status(reply.data, reply.data_len, &state) == JW_RESULT_OK &&
        state.action_execution == JW_EXECUTION_COMPLETE) {
      auto pending = pending_.find(command.channel);
      if (pending != pending_.end() && pending->second.first == state.action_session_id &&
          pending->second.second == state.action_message_id)
        pending_.erase(pending);
    }
  }

  recorded = true;
  return payload;
}

}  // namespace robot::board
