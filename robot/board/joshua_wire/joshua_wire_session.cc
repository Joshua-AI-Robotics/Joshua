// Implements JW session lifecycle, serialization, correlation and ID rotation.
// Only neutral command payloads cross the public API; only JW frames reach comm.
#include "robot/board/joshua_wire/joshua_wire_session.h"

#include <atomic>
#include <random>
#include <utility>

#include "absl/cleanup/cleanup.h"
#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "firmware/common/joshua_wire_sensor_stream.h"
#include "utils/status_macros.h"

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
                                     absl::Duration cyclic_timeout,
                                     ExchangeHook hook,
                                     uint32_t max_attempts)
    : transport_(std::move(transport)),
      cyclic_(std::move(cyclic)),
      cyclic_timeout_(cyclic_timeout),
      source_(source ? std::move(source) : AllocateSessionId),
      hook_(std::move(hook)),
      max_attempts_(max_attempts),
      message_id_limit_(message_id_limit) {}

uint32_t JoshuaWireSession::AllocateSessionId() {
  static const uint32_t seed = std::random_device{}();
  static std::atomic<uint64_t> issued{0};
  for (;;) {
    const auto index = issued.fetch_add(1);
    if (index > UINT32_MAX) return 0;
    const auto value = static_cast<uint32_t>(uint64_t(seed) + index);
    if (value) return value;
  }
}

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
  const bool profile_valid =
      cmd == JW_CMD_PROFILE &&
      jw_decode_profile_request(payload, payload_len, &profile) == JW_RESULT_OK;
  const bool on_cyclic = cyclic_ && profile_valid && profile.profile_id == JW_PROFILE_ACTUATOR &&
                         profile.major == JW_PROFILE_VERSION_MAJOR &&
                         profile.minor == JW_PROFILE_VERSION_MINOR &&
                         (profile.operation == JW_ACTUATOR_SET_TARGET ||
                          profile.operation == JW_ACTUATOR_GET_FEEDBACK);
  jw_frame_t sent{}, received{};
  jw_decode_frame(request.data(), request.size(), &sent);
  absl::Status failure = absl::UnavailableError("JoshuaWire response missing");
  // Retry identical bytes/IDs only. The shared firmware endpoint replays its
  // last reply without repeating side effects, including unknown outcomes.
  std::vector<uint8_t> response;
  bool published = false;
  for (uint32_t attempt = 0; attempt < max_attempts_; ++attempt) {
    ExchangeOutcome outcome;
    if (hook_) {
      outcome = hook_(request);
    } else {
      auto result =
          on_cyclic ? cyclic_->Exchange(request, cyclic_timeout_) : transport_->Exchange(request);
      outcome.status = result.status();
      if (result.ok()) outcome.response = std::move(*result);
    }
    published = published || outcome.published;
    if (!outcome.status.ok()) {
      failure = outcome.status;
      if (!outcome.retryable || absl::IsCancelled(failure)) break;
      continue;
    }
    response = std::move(outcome.response);
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
    if (profile_valid && profile.profile_id == JW_PROFILE_SENSOR &&
        profile.major == JW_PROFILE_VERSION_MAJOR && profile.minor == JW_SENSOR_STREAM_MINOR &&
        reply.status == JW_STATUS_OK && reply.execution == JW_EXECUTION_COMPLETE &&
        (profile.operation == JW_SENSOR_SUBSCRIBE || profile.operation == JW_SENSOR_UNSUBSCRIBE)) {
      jw_stream_state_t state{};
      uint32_t expected = 0;
      const bool create = profile.operation == JW_SENSOR_SUBSCRIBE;
      const bool valid_request =
          create
              ? jw_decode_stream_period(profile.data, profile.data_len, &expected) == JW_RESULT_OK
              : jw_decode_stream_token(profile.data, profile.data_len, &expected) == JW_RESULT_OK;
      if (!valid_request ||
          jw_decode_stream_state(reply.data, reply.data_len, &state) != JW_RESULT_OK ||
          state.active != create ||
          state.subscription_id != (create ? sent.message_id : expected) ||
          (create && state.period_us != expected)) {
        failure = absl::DataLossError("Uncorrelated subscription acknowledgment");
        continue;
      }
    }
    return std::vector<uint8_t>(received.payload, received.payload + received.payload_len);
  }
  if (published) uncertain_ = true;
  return published ? absl::Status(failure.code(),
                                  std::string(failure.message()) + "; command outcome unknown")
                   : failure;
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
  if ((!transport_ && !hook_) || message_id_limit_ < 3 || max_attempts_ == 0 || max_attempts_ > 4 ||
      (cyclic_ &&
       (cyclic_timeout_ <= absl::ZeroDuration() || cyclic_timeout_ == absl::InfiniteDuration()))) {
    return absl::InvalidArgumentError("Invalid JoshuaWire session configuration.");
  }
  // CommFactory provides an open link; Open here starts a protocol session,
  // not a second physical connection or transport lifecycle.
  return ResetLocked();
}

JoshuaWireSession::Snapshot JoshuaWireSession::Inspect() {
  std::lock_guard lock(mutex_);
  return {session_id_, next_message_id_, ready_, uncertain_};
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
  absl::Cleanup invalidate_session = [this] {
    ready_ = false;
    session_id_ = 0;
    pending_.clear();
  };
  return ResetLocked();
}

bool JoshuaWireSession::IsReadOnly(const jw_command_t& command) {
  const bool query = command.cmd == JW_CMD_IDENTIFY || command.cmd == JW_CMD_DISCOVER ||
                     command.cmd == JW_CMD_DESCRIBE_FUNCTION || command.cmd == JW_CMD_GET_STATUS ||
                     command.cmd == JW_CMD_GET_DEPENDENCIES;
  jw_profile_request_t profile{};
  const bool is_profile =
      command.cmd == JW_CMD_PROFILE &&
      jw_decode_profile_request(command.payload, command.payload_len, &profile) == JW_RESULT_OK;
  const bool legacy_read =
      is_profile && profile.major == JW_PROFILE_VERSION_MAJOR &&
      profile.minor == JW_PROFILE_VERSION_MINOR &&
      ((profile.profile_id == JW_PROFILE_ACTUATOR &&
        profile.operation == JW_ACTUATOR_GET_FEEDBACK) ||
       (profile.profile_id == JW_PROFILE_SENSOR && profile.operation == JW_SENSOR_READ) ||
       (profile.profile_id == JW_PROFILE_IO && profile.operation == JW_IO_READ));
  const bool stream_read =
      is_profile && profile.profile_id == JW_PROFILE_SENSOR &&
      profile.major == JW_PROFILE_VERSION_MAJOR && profile.minor == JW_SENSOR_STREAM_MINOR &&
      profile.data_len == 0 &&
      (profile.operation == JW_SENSOR_READ || profile.operation == JW_SENSOR_GET_SUBSCRIPTION);
  return query || legacy_read || stream_read;
}

absl::StatusOr<std::vector<uint8_t>> JoshuaWireSession::Exchange(const jw_command_t& command) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!ready_) return absl::FailedPreconditionError("JoshuaWire requires a successful reset.");
  if (command.payload_len > JW_MAX_PAYLOAD_LEN ||
      (command.payload_len != 0 && command.payload == nullptr) ||
      command.cmd == JW_CMD_RESET_SESSION || command.cmd == JW_CMD_STREAM_SAMPLE) {
    return absl::InvalidArgumentError("Invalid command supplied to JW session.");
  }
  if (next_message_id_ >= message_id_limit_) {
    ABSL_RETURN_IF_ERROR(RotateLocked());
    return absl::FailedPreconditionError(
        "JoshuaWire session rotated; channels disabled. Enable again.");
  }
  if ((uncertain_ || pending_.count(JW_CHANNEL_NONE) || pending_.count(command.channel)) &&
      !IsReadOnly(command) && command.cmd != JW_CMD_ESTOP && command.cmd != JW_CMD_RECOVER)
    return absl::FailedPreconditionError("Reconcile unknown/pending action before mutation.");
  ABSL_ASSIGN_OR_RETURN(
      auto payload,
      ExchangeLocked(command.cmd, command.channel, command.payload, command.payload_len));
  jw_reply_t reply{};
  jw_decode_reply_payload(payload.data(), payload.size(), &reply);
  const bool board_safety = (command.cmd == JW_CMD_ESTOP || command.cmd == JW_CMD_RECOVER) &&
                            command.channel == JW_CHANNEL_NONE && command.payload_len == 2 &&
                            command.payload[0] == 0 && command.payload[1] == 0;
  // Board-wide safety actions replace the firmware's function action tokens.
  // Their confirmed completion retires canceled work, not its historical outcome.
  // Preserve uncertain_: a stop does not prove whether a lost mutation executed.
  if (board_safety && reply.status == JW_STATUS_OK && reply.execution == JW_EXECUTION_COMPLETE)
    pending_.clear();
  if (reply.execution == JW_EXECUTION_ACCEPTED)
    pending_[command.channel] = {session_id_, next_message_id_ - 1, board_safety};
  if (command.cmd == JW_CMD_GET_STATUS && reply.status == JW_STATUS_OK) {
    jw_device_status_t state{};
    if (jw_decode_device_status(reply.data, reply.data_len, &state) == JW_RESULT_OK &&
        state.action_execution == JW_EXECUTION_COMPLETE) {
      auto pending = pending_.find(command.channel);
      if (pending != pending_.end() && pending->second.session_id == state.action_session_id &&
          pending->second.message_id == state.action_message_id) {
        if (pending->second.cancels_functions && state.action_status == JW_STATUS_OK)
          pending_.clear();
        else
          pending_.erase(pending);
      }
    }
  }

  return payload;
}

}  // namespace robot::board
