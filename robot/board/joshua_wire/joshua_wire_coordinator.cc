#include "robot/board/joshua_wire/joshua_wire_coordinator.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <tuple>
#include <utility>

#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "firmware/common/joshua_wire_sensor_stream.h"
#include "robot/board/joshua_wire/joshua_wire_session.h"
#include "utils/status_macros.h"

namespace robot::board {
namespace {
using Link = robot::comm::FrameStreamTransport;
using Clock = Link::Clock;
using Ms = std::chrono::milliseconds;
using Bytes = std::vector<uint8_t>;
// Software scheduling class, never supplied by an application to bypass quotas.
int Priority(const jw_command_t& command) {
  if (command.cmd == JW_CMD_ESTOP) return 0;
  if (command.cmd == JW_CMD_GET_STATUS || command.cmd == JW_CMD_IDENTIFY ||
      command.cmd == JW_CMD_DISCOVER || command.cmd == JW_CMD_DESCRIBE_FUNCTION ||
      command.cmd == JW_CMD_GET_DEPENDENCIES)
    return 3;
  jw_profile_request_t profile{};
  if (command.cmd == JW_CMD_PROFILE &&
      jw_decode_profile_request(command.payload, command.payload_len, &profile) == JW_RESULT_OK) {
    if (profile.profile_id == JW_PROFILE_ACTUATOR &&
        (profile.operation == JW_ACTUATOR_SET_TARGET || profile.operation == JW_ACTUATOR_DISABLE))
      return 1;
    if ((profile.profile_id == JW_PROFILE_ACTUATOR &&
         profile.operation == JW_ACTUATOR_GET_FEEDBACK) ||
        (profile.profile_id == JW_PROFILE_SENSOR &&
         (profile.operation == JW_SENSOR_READ ||
          profile.operation == JW_SENSOR_GET_SUBSCRIPTION)) ||
        (profile.profile_id == JW_PROFILE_IO && profile.operation == JW_IO_READ))
      return 3;
  }
  return 2;
}
}  // namespace

struct JoshuaWireCoordinator::Operation::State {
  uint8_t command = 0, function = JW_CHANNEL_NONE;
  Bytes payload;
  int priority = 2;
  size_t reservation = 0;
  Clock::time_point deadline;
  std::atomic<bool> cancel{false};
  std::weak_ptr<Impl> owner;
  mutable std::mutex mutex;
  std::condition_variable changed;
  bool done = false;
  bool internal = false;
  Result result;
};

struct JoshuaWireCoordinator::Subscription::State {
  uint8_t function = 0;
  uint32_t session = 0, token = 0, period = 0, boot = 0;
  jw_sensor_schema_t schema{};
  size_t consumers = 0;
  bool active = false, retired = false;
  std::optional<Observation> latest;
};

struct JoshuaWireCoordinator::Impl {
  using Work = std::shared_ptr<Operation::State>;
  std::shared_ptr<Link> link;
  const CoordinatorPolicy policy;
  JoshuaWireSession session;
  mutable std::mutex mutex;
  std::mutex close_mutex;
  std::timed_mutex subscription_mutex;  // Caller management only; never held by worker.
  std::map<uint8_t, std::shared_ptr<Subscription::State>> streams;
  std::array<std::deque<Work>, 4> queues;
  Work active;
  Work startup = std::make_shared<Operation::State>();
  std::thread worker;
  std::thread::id worker_id;
  bool running = false, closing = false;
  absl::Status failure, close_status;
  Diagnostics diagnostics;
  uint32_t burst = 0;
  std::deque<Link::ReceivedFrame> notifications;
  uint32_t routed_session = 0;
  // Accessed by the worker only, including the session's scheduling hook.
  uint32_t last_attempt_session = 0, last_attempt_message = 0;
  Clock::time_point next_heartbeat = Clock::time_point::max();

  Impl(std::shared_ptr<Link> connection,
       CoordinatorPolicy p,
       JoshuaWireSession::SessionIdSource source)
      : link(std::move(connection)),
        policy(p),
        session(
            nullptr,
            std::move(source),
            UINT32_MAX,
            nullptr,
            absl::ZeroDuration(),
            [this](absl::Span<const uint8_t> bytes) { return ExchangeFrame(bytes); },
            p.max_attempts) {}

  void RetireStreamsLocked() {
    for (auto& [id, stream] : streams) {
      if (!stream->retired) ++diagnostics.retired_streams;
      stream->retired = true;
      stream->active = false;
      stream->latest.reset();
    }
    diagnostics.active_streams = 0;
  }
  void Wake() {
    std::shared_ptr<Link> connection;
    {
      std::lock_guard lock(mutex);
      connection = link;
    }
    if (connection) connection->Interrupt();
  }
  // Called with mutex held. Returns true on a boot discontinuity.
  bool StoreSample(const std::shared_ptr<Subscription::State>& stream,
                   const Link::ReceivedFrame& frame) {
    if (!stream->active || stream->retired) return false;
    if (frame.received_at > Clock::now()) {
      ++diagnostics.invalid_samples;
      return false;
    }
    jw_frame_t envelope{};
    if (jw_decode_frame(frame.bytes.data(), frame.bytes.size(), &envelope) != JW_RESULT_OK ||
        envelope.session_id != stream->session || envelope.message_id != stream->token ||
        envelope.channel != stream->function)
      return false;
    jw_stream_sample_t sample{};
    if (jw_decode_stream_sample(frame.bytes.data(), frame.bytes.size(), &sample) != JW_RESULT_OK) {
      ++diagnostics.invalid_samples;
      return false;
    }
    if (sample.session_id != stream->session || sample.subscription_id != stream->token ||
        sample.function_id != stream->function)
      return false;
    if (sample.sample.metadata.boot_id != stream->boot) {
      ++diagnostics.boot_discontinuities;
      return true;
    }
    if (sample.sample.metadata.clock != stream->schema.clock) {
      ++diagnostics.invalid_samples;
      return false;
    }
    uint64_t gaps = 0, overwritten = 0;
    if (stream->latest) {
      auto& old = *stream->latest;
      const auto& a = old.sample.sample.metadata;
      const auto& b = sample.sample.metadata;
      const uint32_t delta = b.sequence - a.sequence;
      if (delta == 0) {
        const bool same_value =
            sample.sample.value == old.sample.sample.value ||
            (std::isnan(sample.sample.value) && std::isnan(old.sample.sample.value));
        if (b.timestamp_us != a.timestamp_us || !same_value ||
            sample.dropped_acquisitions < old.sample.dropped_acquisitions) {
          ++diagnostics.invalid_samples;
          return false;
        }
        // Validity/loss reporting may change without a new acquisition; do not
        // change the original receipt time or manufacture a new sample age.
        old.sample = sample;
        return false;
      }
      if (delta > INT32_MAX || b.timestamp_us < a.timestamp_us ||
          sample.dropped_acquisitions < old.sample.dropped_acquisitions) {
        ++diagnostics.invalid_samples;
        return false;
      }
      gaps = old.sequence_gaps + delta - 1;
      overwritten = old.overwritten + 1;
      diagnostics.sequence_gaps += delta - 1;
    }
    stream->latest = Observation{
        sample, stream->schema, frame.received_at, std::chrono::microseconds(0), gaps, overwritten};
    return false;
  }
  bool OnWorker() const {
    return running && std::this_thread::get_id() == worker_id;
  }
  void Complete(const Work& work, absl::Status status, bool counted = true) {
    if (counted && !work->internal) {
      std::lock_guard lock(mutex);
      if (work->priority == 0)
        diagnostics.stop_pending = false;
      else {
        --diagnostics.pending_operations;
        diagnostics.pending_bytes -= work->reservation;
      }
      ++diagnostics.completed;
      if (absl::IsCancelled(status)) ++diagnostics.cancelled;
      if (absl::IsDeadlineExceeded(status)) ++diagnostics.timed_out;
      if (!status.ok()) diagnostics.last_error = status.ToString().substr(0, 512);
    }
    work->payload.clear();
    {
      std::lock_guard lock(work->mutex);
      work->result.status = std::move(status);
      work->done = true;
    }
    work->changed.notify_all();
  }
  void ExpireQueued() {
    std::vector<Work> expired;
    {
      std::lock_guard lock(mutex);
      for (auto& queue : queues) {
        for (auto it = queue.begin(); it != queue.end();) {
          if ((*it)->cancel || Clock::now() >= (*it)->deadline) {
            expired.push_back(*it);
            it = queue.erase(it);
          } else
            ++it;
        }
      }
    }
    for (const auto& work : expired)
      Complete(work,
               work->cancel ? absl::CancelledError("Queued command canceled; not sent")
                            : absl::DeadlineExceededError("Queued command expired; not sent"));
  }
  Clock::time_point NextQueueDeadline(Clock::time_point limit) {
    std::lock_guard lock(mutex);
    for (const auto& queue : queues)
      for (const auto& work : queue) limit = std::min(limit, work->deadline);
    return limit;
  }
  void Fault(absl::Status status) {
    {
      std::lock_guard lock(mutex);
      if (failure.ok()) failure = std::move(status);
      diagnostics.faulted = true;
      diagnostics.ready = false;
      diagnostics.last_error = failure.ToString().substr(0, 512);
      closing = true;
      if (active) active->cancel = true;
      RetireStreamsLocked();
    }
    Wake();
  }
  void RouteNotification(Link::ReceivedFrame frame) {
    jw_frame_t decoded{};
    if (jw_decode_frame(frame.bytes.data(), frame.bytes.size(), &decoded) != JW_RESULT_OK) return;
    std::unique_lock lock(mutex);
    if (decoded.session_id != routed_session) {
      ++diagnostics.stale_notifications;
      return;
    }
    ++diagnostics.notifications;
    auto old = std::find_if(notifications.begin(), notifications.end(), [&](const auto& value) {
      jw_frame_t previous{};
      jw_decode_frame(value.bytes.data(), value.bytes.size(), &previous);
      return previous.message_id == decoded.message_id && previous.channel == decoded.channel;
    });
    if (old != notifications.end()) {
      notifications.erase(old);
      ++diagnostics.notifications_dropped;
    }
    if (notifications.size() == policy.max_notifications) {
      notifications.pop_front();
      ++diagnostics.notifications_dropped;
    }
    auto found = streams.find(decoded.channel);
    const bool reboot = found != streams.end() && StoreSample(found->second, frame);
    notifications.push_back(std::move(frame));
    lock.unlock();
    if (reboot)
      Fault(absl::UnavailableError("MCU boot changed; all handles require deliberate reconnect"));
  }
  JoshuaWireSession::ExchangeOutcome ExchangeFrame(absl::Span<const uint8_t> bytes) {
    JoshuaWireSession::ExchangeOutcome outcome;
    outcome.published = false;
    jw_frame_t sent{};
    if (jw_decode_frame(bytes.data(), bytes.size(), &sent) != JW_RESULT_OK) {
      outcome.status = absl::InvalidArgumentError("Session produced an invalid frame");
      outcome.retryable = false;
      return outcome;
    }
    Work work;
    {
      std::lock_guard lock(mutex);
      work = active;
      if (last_attempt_session == sent.session_id && last_attempt_message == sent.message_id)
        ++diagnostics.retries;
      else if (sent.cmd == JW_CMD_RESET_SESSION) {
        ++diagnostics.resets;
        routed_session = sent.session_id;
        notifications.clear();
      }
      last_attempt_session = sent.session_id;
      last_attempt_message = sent.message_id;
    }
    work->result.session_id = sent.session_id;
    work->result.message_id = sent.message_id;
    auto interrupted = [&] {
      if (work->cancel) {
        outcome.status = absl::CancelledError("Command retired before next frame publication");
        outcome.retryable = false;
        return true;
      }
      if (Clock::now() >= work->deadline) {
        outcome.status = absl::DeadlineExceededError("Complete command budget expired");
        outcome.retryable = false;
        return true;
      }
      return false;
    };
    if (interrupted()) return outcome;
    const auto attempt_deadline =
        std::min(work->deadline, Clock::now() + Ms(policy.attempt_timeout_ms));
    auto written = link->WriteFrame(bytes, attempt_deadline);
    outcome.published = written.bytes_written > 0;
    work->result.published = work->result.published || outcome.published;
    if (!written.status.ok()) {
      outcome.status = written.status;
      outcome.retryable = !work->cancel && Clock::now() < work->deadline && link->status().ok();
      if (!link->status().ok()) Fault(link->status());
      return outcome;
    }
    while (!interrupted()) {
      ExpireQueued();
      if (Clock::now() >= attempt_deadline) break;
      auto frame = link->Receive(NextQueueDeadline(attempt_deadline));
      if (!frame.ok()) {
        if (absl::IsDeadlineExceeded(frame.status()) ||
            (absl::IsCancelled(frame.status()) && link->status().ok()))
          continue;
        outcome.status = frame.status();
        outcome.retryable = false;
        Fault(frame.status());
        return outcome;
      }
      jw_frame_t received{};
      if (jw_decode_frame(frame->bytes.data(), frame->bytes.size(), &received) == JW_RESULT_OK) {
        if (received.cmd == JW_CMD_STREAM_SAMPLE) {
          RouteNotification(std::move(*frame));
          continue;
        }
        if (jw_response_matches(&sent, &received)) {
          outcome.response = std::move(frame->bytes);
          outcome.status = absl::OkStatus();
          return outcome;
        }
      }
      {
        std::lock_guard lock(mutex);
        ++diagnostics.stale_replies;
      }
    }
    if (outcome.status.ok()) outcome.status = absl::DeadlineExceededError("Reply attempt deadline");
    return outcome;
  }
  Work SelectLocked() {
    if (!queues[0].empty()) {
      auto work = queues[0].front();
      queues[0].pop_front();
      return work;
    }
    int selected = -1;
    for (int i = 1; i < 4; ++i)
      if (!queues[i].empty()) {
        selected = i;
        break;
      }
    if (selected < 0) return {};
    // Lower-priority polling/configuration may consume slack after a bounded
    // burst. If its entire remaining budget does not fit, keep deadline control.
    if (burst >= policy.max_priority_burst) {
      for (int i = 3; i > selected; --i) {
        if (queues[i].empty()) continue;
        if (queues[selected].front()->deadline >
            queues[i].front()->deadline + Ms(policy.scheduling_guard_ms)) {
          selected = i;
          burst = 0;
          break;
        }
      }
    }
    if (selected == 1 || selected == 2)
      ++burst;
    else
      burst = 0;
    auto work = queues[selected].front();
    queues[selected].pop_front();
    return work;
  }
  void UpdateSession() {
    const auto snapshot = session.Inspect();
    std::lock_guard lock(mutex);
    diagnostics.ready = snapshot.ready && !closing;
    diagnostics.uncertain = snapshot.uncertain;
    diagnostics.session_id = snapshot.session_id;
  }
  void Run() {
    {
      std::lock_guard lock(mutex);
      active = startup;
    }
    try {
      auto status = session.Open();
      UpdateSession();
      {
        std::lock_guard lock(mutex);
        active.reset();
      }
      Complete(startup, status, false);
      if (!status.ok()) Fault(status);
      if (policy.heartbeat_period_ms)
        next_heartbeat = Clock::now() + Ms(policy.heartbeat_period_ms);
      for (;;) {
        ExpireQueued();
        Work work;
        bool heartbeat = false;
        {
          std::lock_guard lock(mutex);
          if (closing) break;
          if (policy.heartbeat_period_ms && Clock::now() >= next_heartbeat && queues[0].empty()) {
            // Independent admission: ordinary producers cannot consume this slot.
            // A stop still retires it through the same frame-boundary mechanism.
            work = std::make_shared<Operation::State>();
            work->internal = true;
            work->command = JW_CMD_GET_STATUS;
            work->priority = 3;
            work->deadline = Clock::now() + Ms(policy.polling_timeout_ms);
            heartbeat = true;
          } else {
            work = SelectLocked();
          }
          active = work;
        }
        if (!work) {
          auto frame = link->Receive(next_heartbeat);
          if (!frame.ok()) {
            if (absl::IsDeadlineExceeded(frame.status())) continue;
            if (absl::IsCancelled(frame.status()) && link->status().ok()) continue;
            Fault(frame.status());
            break;
          }
          jw_frame_t incoming{};
          if (jw_decode_frame(frame->bytes.data(), frame->bytes.size(), &incoming) ==
                  JW_RESULT_OK &&
              incoming.cmd == JW_CMD_STREAM_SAMPLE)
            RouteNotification(std::move(*frame));
          else {
            std::lock_guard lock(mutex);
            ++diagnostics.stale_replies;
          }
          continue;
        }
        absl::Status result;
        if (work->cancel)
          result = absl::CancelledError("Command canceled before dispatch");
        else if (Clock::now() >= work->deadline)
          result = absl::DeadlineExceededError("Command expired before dispatch");
        else {
          jw_command_t command{
              0, 0, work->command, work->function, work->payload.data(), work->payload.size()};
          bool interlock;
          {
            std::lock_guard lock(mutex);
            interlock = diagnostics.safety_interlock;
          }
          if (interlock && !JoshuaWireSession::IsReadOnly(command) && command.cmd != JW_CMD_ESTOP &&
              command.cmd != JW_CMD_RECOVER)
            result = absl::FailedPreconditionError(
                "Host safety interlock requires deliberate disabled reset/reconnect");
          else {
            auto response = session.Exchange(command);
            result = response.status();
            if (response.ok()) work->result.payload = std::move(*response);
            if (heartbeat && result.ok()) {
              jw_reply_t reply{};
              jw_device_status_t state{};
              if (jw_decode_reply_payload(work->result.payload.data(),
                                          work->result.payload.size(),
                                          &reply) != JW_RESULT_OK ||
                  reply.status != JW_STATUS_OK || reply.execution != JW_EXECUTION_COMPLETE ||
                  jw_decode_device_status(reply.data, reply.data_len, &state) != JW_RESULT_OK ||
                  state.state == JW_STATE_FAULT || state.reason == JW_REASON_DEVICE_FAULT ||
                  (state.recovery & JW_RECOVERY_CLEAR_ESTOP))
                result =
                    absl::FailedPreconditionError("Heartbeat reports invalid/faulted MCU state");
            }
          }
        }
        UpdateSession();
        {
          std::lock_guard lock(mutex);
          active.reset();
        }
        Complete(work, result, !heartbeat);
        if (heartbeat) {
          next_heartbeat = Clock::now() + Ms(policy.heartbeat_period_ms);
          {
            std::lock_guard lock(mutex);
            ++diagnostics.heartbeats;
            if (!result.ok() && !work->cancel) ++diagnostics.heartbeat_failures;
          }
          if (!result.ok() && !work->cancel) Fault(result);
        }
      }
    } catch (...) {
      Fault(absl::InternalError("Coordinator operation threw"));
      Work current;
      {
        std::lock_guard lock(mutex);
        current = active;
      }
      if (current && !current->done) {
        // A throwing backend cannot establish that publication never occurred.
        current->result.published = true;
        {
          std::lock_guard lock(mutex);
          diagnostics.uncertain = true;
        }
        Complete(current, failure, current != startup);
      }
      if (!startup->done) Complete(startup, failure, false);
    }
    std::vector<Work> abandoned;
    {
      std::lock_guard lock(mutex);
      for (auto& queue : queues) {
        for (auto& work : queue) abandoned.push_back(work);
        queue.clear();
      }
      notifications.clear();
    }
    for (const auto& work : abandoned)
      Complete(work, failure.ok() ? absl::CancelledError("Coordinator closed") : failure);
    // Deliberate terminal teardown: fresh reset disables outputs, then physical
    // close. Never restore settings, clear ESTOP, or replay targets.
    auto shutdown = std::make_shared<Operation::State>();
    shutdown->deadline = Clock::now() + Ms(policy.operation_timeout_ms);
    {
      std::lock_guard lock(mutex);
      active = shutdown;
    }
    try {
      close_status = link->status().ok() ? session.Close() : link->status();
    } catch (...) {
      close_status = absl::InternalError("Session teardown threw");
    }
    const auto closed = link->Close();
    if (close_status.ok()) close_status = closed;
    std::lock_guard lock(mutex);
    active.reset();
    notifications.clear();
    diagnostics.closed = true;
    diagnostics.ready = false;
    running = false;
  }
};

absl::StatusOr<std::shared_ptr<JoshuaWireCoordinator>> JoshuaWireCoordinator::Acquire(
    std::shared_ptr<Link> link,
    const JoshuaWireHostPolicy& config,
    JoshuaWireSession::SessionIdSource source) {
  auto policy = ResolveCoordinatorPolicy(config);
  auto valid = ValidateCoordinatorPolicy(policy);
  if (!valid.ok()) return valid;
  if (!link) return absl::InvalidArgumentError("Coordinator requires a duplex link");
  static auto* mutex = new std::mutex;
  struct Entry {
    std::weak_ptr<JoshuaWireCoordinator> owner;
    std::weak_ptr<Impl> reservation;
    std::weak_ptr<Link> connection;
  };
  static auto* cache = new std::map<Link*, Entry>;
  std::lock_guard lock(*mutex);
  for (auto it = cache->begin(); it != cache->end();) {
    if (it->second.connection.expired() ||
        (it->second.owner.expired() && it->second.reservation.expired()))
      it = cache->erase(it);
    else
      ++it;
  }
  if (auto found = cache->find(link.get()); found != cache->end()) {
    if (auto existing = found->second.owner.lock()) {
      if (existing->impl_->policy != policy)
        return absl::InvalidArgumentError("MCU coordinator policy differs");
      const auto snapshot = existing->Snapshot();
      if (!snapshot.ready || snapshot.closed || snapshot.faulted)
        return absl::FailedPreconditionError("MCU lease requires deliberate close/reconnect");
      return existing;
    }
    return absl::AlreadyExistsError("Previous MCU coordinator is still retiring");
  }
  auto status = link->status();
  if (!status.ok()) return status;
  auto state = std::make_shared<Impl>(link, policy, std::move(source));
  auto owner = std::shared_ptr<JoshuaWireCoordinator>(new JoshuaWireCoordinator(state));
  (*cache)[link.get()] = {owner, state, link};
  state->startup->deadline = Clock::now() + Ms(policy.operation_timeout_ms);
  state->running = true;
  state->worker = std::thread([state] { state->Run(); });
  state->worker_id = state->worker.get_id();
  const auto startup = Operation(state->startup).Wait();
  if (!startup.status.ok()) return startup.status;
  return owner;
}
JoshuaWireCoordinator::~JoshuaWireCoordinator() {
  Close().IgnoreError();
}
JoshuaWireCoordinator::Operation JoshuaWireCoordinator::Submit(const jw_command_t& command,
                                                               Clock::time_point deadline) {
  const auto admitted = Clock::now();
  auto work = std::make_shared<Operation::State>();
  work->command = command.cmd;
  work->function = command.channel;
  absl::Status rejected;
  if (command.payload_len > JW_MAX_PAYLOAD_LEN || (command.payload_len && !command.payload) ||
      command.cmd == JW_CMD_RESET_SESSION || command.cmd == JW_CMD_STREAM_SAMPLE)
    rejected = absl::InvalidArgumentError("Invalid coordinator command");
  else {
    if (command.payload_len)
      work->payload.assign(command.payload, command.payload + command.payload_len);
    work->priority = Priority(command);
    work->reservation = JW_FRAME_OVERHEAD + work->payload.size() + JW_MAX_FRAME_LEN;
  }
  std::unique_lock lock(impl_->mutex);
  const auto budget =
      work->priority == 3 ? impl_->policy.polling_timeout_ms : impl_->policy.operation_timeout_ms;
  work->deadline = std::min(deadline, admitted + Ms(budget));
  if (rejected.ok() && impl_->OnWorker())
    rejected = absl::FailedPreconditionError("Coordinator cannot wait on itself");
  if (rejected.ok() && impl_->closing)
    rejected =
        impl_->failure.ok() ? absl::FailedPreconditionError("Coordinator closed") : impl_->failure;
  if (rejected.ok()) {
    if ((work->priority == 0 && impl_->diagnostics.stop_pending) ||
        (work->priority != 0 &&
         (impl_->diagnostics.pending_operations >= impl_->policy.max_pending_operations ||
          work->reservation > impl_->policy.max_pending_bytes - impl_->diagnostics.pending_bytes)))
      rejected = absl::ResourceExhaustedError("Coordinator admission full");
  }
  if (!rejected.ok()) {
    ++impl_->diagnostics.rejected;
    work->result.status = rejected;
    work->done = true;
    return Operation(work);
  }
  work->owner = impl_;
  impl_->queues[work->priority].push_back(work);
  ++impl_->diagnostics.admitted;
  if (work->priority == 0) {
    impl_->diagnostics.stop_pending = true;
    impl_->diagnostics.safety_interlock = true;
    impl_->RetireStreamsLocked();
    if (impl_->active && impl_->active->priority != 0) {
      impl_->active->cancel = true;
      ++impl_->diagnostics.retired;
    }
  } else {
    ++impl_->diagnostics.pending_operations;
    impl_->diagnostics.pending_bytes += work->reservation;
  }
  lock.unlock();
  impl_->Wake();
  return Operation(work);
}
absl::StatusOr<Bytes> JoshuaWireCoordinator::Exchange(const jw_command_t& command,
                                                      Clock::time_point deadline) {
  auto result = Submit(command, deadline).Wait();
  if (!result.status.ok()) return result.status;
  return std::move(result.payload);
}
void JoshuaWireCoordinator::Operation::Cancel() const {
  {
    std::lock_guard lock(state_->mutex);
    if (state_->done) return;
  }
  state_->cancel = true;
  if (auto owner = state_->owner.lock()) owner->Wake();
}
bool JoshuaWireCoordinator::Operation::Ready() const {
  std::lock_guard lock(state_->mutex);
  return state_->done;
}
JoshuaWireCoordinator::Result JoshuaWireCoordinator::Operation::Wait() const {
  std::unique_lock lock(state_->mutex);
  state_->changed.wait(lock, [&] { return state_->done; });
  return state_->result;
}
JoshuaWireCoordinator::Diagnostics JoshuaWireCoordinator::Snapshot() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostics;
}
std::vector<Link::ReceivedFrame> JoshuaWireCoordinator::DrainNotifications() {
  std::lock_guard lock(impl_->mutex);
  std::vector<Link::ReceivedFrame> result;
  while (!impl_->notifications.empty()) {
    result.push_back(std::move(impl_->notifications.front()));
    impl_->notifications.pop_front();
  }
  return result;
}
absl::Status JoshuaWireCoordinator::Close() {
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->OnWorker()) return absl::FailedPreconditionError("Coordinator cannot close itself");
  }
  std::lock_guard close_lock(impl_->close_mutex);
  {
    std::lock_guard lock(impl_->mutex);
    impl_->closing = true;
    impl_->diagnostics.ready = false;
    impl_->RetireStreamsLocked();
    if (impl_->active) impl_->active->cancel = true;
  }
  impl_->Wake();
  if (impl_->worker.joinable()) impl_->worker.join();
  {
    std::lock_guard lock(impl_->mutex);
    impl_->link.reset();
  }
  return impl_->close_status;
}
namespace {
absl::StatusOr<Bytes> ReplyData(const JoshuaWireCoordinator::Result& result) {
  if (!result.status.ok()) return result.status;
  jw_reply_t reply{};
  if (jw_decode_reply_payload(result.payload.data(), result.payload.size(), &reply) != JW_RESULT_OK)
    return absl::DataLossError("Malformed stream management reply");
  if (reply.status != JW_STATUS_OK || reply.execution != JW_EXECUTION_COMPLETE) {
    switch (reply.status) {
      case JW_STATUS_UNSUPPORTED:
        return absl::UnimplementedError("Sensor streaming unsupported");
      case JW_STATUS_BUSY:
        return absl::ResourceExhaustedError("Sensor subscription busy");
      case JW_STATUS_INVALID_PAYLOAD:
        return absl::InvalidArgumentError("Rejected sensor period/payload");
      case JW_STATUS_NOT_READY:
      case JW_STATUS_UNAVAILABLE:
        return absl::UnavailableError("Sensor unavailable");
      case JW_STATUS_OUTCOME_UNKNOWN:
        return absl::UnknownError("Subscription outcome unknown; reconcile disabled");
      default:
        return absl::FailedPreconditionError("Sensor management did not complete");
    }
  }
  return Bytes(reply.data, reply.data + reply.data_len);
}
absl::StatusOr<Bytes> QueryData(JoshuaWireCoordinator& owner,
                                uint8_t command,
                                uint8_t function,
                                Clock::time_point deadline,
                                const uint8_t* data = nullptr,
                                size_t size = 0) {
  return ReplyData(owner.Submit({0, 0, command, function, data, size}, deadline).Wait());
}
Bytes StreamRequest(uint8_t operation, const uint8_t* data = nullptr, size_t size = 0) {
  Bytes payload(JW_PROFILE_HEADER_LEN + size);
  jw_profile_request_t profile{
      JW_PROFILE_SENSOR, JW_PROFILE_VERSION_MAJOR, JW_SENSOR_STREAM_MINOR, operation, data, size};
  const int length = jw_encode_profile_request(payload.data(), payload.size(), &profile);
  if (length < 0) return {};
  return payload;
}
struct SensorDescription {
  uint32_t boot;
  jw_sensor_schema_t schema;
};
absl::StatusOr<SensorDescription> DescribeSensor(JoshuaWireCoordinator& owner,
                                                 uint8_t function,
                                                 Clock::time_point deadline,
                                                 uint32_t response_budget_ms) {
  ABSL_ASSIGN_OR_RETURN(auto first, QueryData(owner, JW_CMD_IDENTIFY, JW_CHANNEL_NONE, deadline));
  jw_identify_response_t identity{};
  if (jw_decode_identify_payload(first.data(), first.size(), &identity) != JW_RESULT_OK ||
      !(identity.supported_planes & JW_PLANE_MESSAGE) ||
      !(identity.safety_flags & JW_SAFETY_BOARD_ESTOP) ||
      identity.max_handler_us > uint64_t(response_budget_ms) * 1000)
    return absl::FailedPreconditionError("Incompatible sensor MCU identity");
  uint16_t cursor = 0;
  uint32_t generation = 0;
  size_t count = 0;
  int previous_id = -1;
  std::optional<jw_function_descriptor_t> selected;
  do {
    uint8_t query[6];
    for (int i = 0; i < 4; ++i) query[i] = generation >> (i * 8);
    query[4] = cursor;
    query[5] = cursor >> 8;
    ABSL_ASSIGN_OR_RETURN(
        auto data,
        QueryData(owner, JW_CMD_DISCOVER, JW_CHANNEL_NONE, deadline, query, sizeof(query)));
    jw_discovery_page_t page{};
    if (jw_decode_discovery_page(data.data(), data.size(), &page) != JW_RESULT_OK ||
        page.generation != identity.discovery_generation || page.total != identity.function_count ||
        (page.next_cursor != JW_CURSOR_END &&
         (!page.count || page.next_cursor != cursor + page.count)))
      return absl::DataLossError("Inconsistent sensor discovery");
    for (uint8_t i = 0; i < page.count; ++i) {
      if (page.functions[i].function_id <= previous_id)
        return absl::DataLossError("Unsorted sensor catalog");
      previous_id = page.functions[i].function_id;
      if (page.functions[i].function_id == function) selected = page.functions[i];
    }
    count += page.count;
    generation = page.generation;
    cursor = page.next_cursor;
    if (count > JW_MAX_FUNCTIONS) return absl::DataLossError("Unbounded sensor discovery");
  } while (cursor != JW_CURSOR_END);
  if (count != identity.function_count) return absl::DataLossError("Incomplete sensor catalog");
  if (!selected || !jw_sensor_supports_stream(&*selected))
    return absl::UnimplementedError("Function does not advertise sensor streaming 1.1");
  ABSL_ASSIGN_OR_RETURN(auto metadata,
                        QueryData(owner, JW_CMD_DESCRIBE_FUNCTION, function, deadline));
  uint8_t expected[JW_FUNCTION_DESCRIPTOR_LEN];
  jw_encode_function_descriptor(expected, sizeof(expected), &*selected);
  jw_sensor_schema_t schema{};
  if (metadata.size() < sizeof(expected) ||
      !std::equal(expected, expected + sizeof(expected), metadata.begin()) ||
      jw_decode_sensor_schema(metadata.data() + sizeof(expected),
                              metadata.size() - sizeof(expected),
                              &schema) != JW_RESULT_OK)
    return absl::DataLossError("Unsupported or changed sensor schema");
  if (schema.unit > JW_UNIT_METRE && schema.unit != JW_UNIT_NATIVE)
    return absl::UnimplementedError("Unknown sensor unit");
  ABSL_ASSIGN_OR_RETURN(auto last, QueryData(owner, JW_CMD_IDENTIFY, JW_CHANNEL_NONE, deadline));
  jw_identify_response_t current{};
  if (jw_decode_identify_payload(last.data(), last.size(), &current) != JW_RESULT_OK ||
      current.boot_id != identity.boot_id ||
      current.discovery_generation != identity.discovery_generation)
    return absl::UnavailableError("Sensor MCU changed during discovery");
  return SensorDescription{identity.boot_id, schema};
}
}  // namespace

absl::StatusOr<std::shared_ptr<JoshuaWireCoordinator::Subscription>>
JoshuaWireCoordinator::Subscribe(uint8_t function, uint32_t period_us) {
  const auto deadline = Clock::now() + Ms(impl_->policy.operation_timeout_ms);
  if (function == JW_CHANNEL_NONE || period_us == 0)
    return absl::InvalidArgumentError("Invalid sensor function/period");
  std::unique_lock management(impl_->subscription_mutex, std::defer_lock);
  if (!management.try_lock_until(deadline))
    return absl::DeadlineExceededError("Subscription management busy");
  const uint64_t required =
      (uint64_t(JW_STREAM_SAMPLE_FRAME_LEN) * 1000000 + period_us - 1) / period_us;
  {
    std::lock_guard lock(impl_->mutex);
    if (impl_->closing || !impl_->diagnostics.ready)
      return absl::UnavailableError("MCU subscription generation closed");
    auto found = impl_->streams.find(function);
    if (found != impl_->streams.end()) {
      auto entry = found->second;
      if (!entry->active || entry->retired)
        return absl::FailedPreconditionError(
            "Retired/uncertain subscription requires disabled reconnect");
      if (entry->period != period_us)
        return absl::InvalidArgumentError("Consumers must agree on subscription period");
      ++entry->consumers;
      return std::shared_ptr<Subscription>(new Subscription(shared_from_this(), entry));
    }
    if (impl_->streams.size() >= impl_->policy.max_streams)
      return absl::ResourceExhaustedError("MCU stream capacity exhausted");
    uint64_t reserved = 0;
    for (const auto& [id, entry] : impl_->streams)
      reserved +=
          (uint64_t(JW_STREAM_SAMPLE_FRAME_LEN) * 1000000 + entry->period - 1) / entry->period;
    if (reserved + required > impl_->policy.telemetry_bytes_per_second)
      return absl::ResourceExhaustedError("Subscription exceeds configured telemetry reservation");
  }
  ABSL_ASSIGN_OR_RETURN(
      auto sensor,
      DescribeSensor(*this,
                     function,
                     deadline,
                     std::min(impl_->policy.attempt_timeout_ms, impl_->policy.polling_timeout_ms)));
  auto entry = std::make_shared<Subscription::State>();
  entry->function = function;
  entry->period = period_us;
  entry->boot = sensor.boot;
  entry->schema = sensor.schema;
  {
    std::lock_guard lock(impl_->mutex);
    impl_->streams[function] = entry;
  }
  uint8_t period[JW_STREAM_PERIOD_LEN];
  jw_encode_stream_period(period, sizeof(period), period_us);
  const auto payload = StreamRequest(JW_SENSOR_SUBSCRIBE, period, sizeof(period));
  const auto operation =
      Submit({0, 0, JW_CMD_PROFILE, function, payload.data(), payload.size()}, deadline).Wait();
  auto data = ReplyData(operation);
  jw_stream_state_t state{};
  const bool valid =
      data.ok() && jw_decode_stream_state(data->data(), data->size(), &state) == JW_RESULT_OK &&
      state.active && state.subscription_id == operation.message_id && state.period_us == period_us;
  bool reboot = false;
  {
    std::lock_guard lock(impl_->mutex);
    entry->session = operation.session_id;
    entry->token = operation.message_id;
    if (!valid || impl_->closing) {
      // Known rejection or unsent failure has no subscription effect. A lost or
      // malformed successful acknowledgment reserves the uncertain identity.
      jw_reply_t reply{};
      const bool rejected =
          operation.status.ok() &&
          jw_decode_reply_payload(operation.payload.data(), operation.payload.size(), &reply) ==
              JW_RESULT_OK &&
          reply.execution == JW_EXECUTION_REJECTED;
      if (!operation.published || rejected) {
        if (entry->retired) --impl_->diagnostics.retired_streams;
        impl_->streams.erase(function);
      } else {
        if (!entry->retired) ++impl_->diagnostics.retired_streams;
        entry->retired = true;
      }
      return data.ok() ? absl::DataLossError("Subscription acknowledgment/generation invalid")
                       : data.status();
    }
    entry->session = operation.session_id;
    entry->token = state.subscription_id;
    entry->active = true;
    entry->consumers = 1;
    ++impl_->diagnostics.active_streams;
    for (const auto& cached : impl_->notifications)
      reboot = impl_->StoreSample(entry, cached) || reboot;
  }
  if (reboot) {
    impl_->Fault(absl::UnavailableError("Sensor boot changed before subscription acknowledgment"));
    return absl::UnavailableError("Sensor boot discontinuity");
  }
  return std::shared_ptr<Subscription>(new Subscription(shared_from_this(), entry));
}

JoshuaWireCoordinator::Subscription::~Subscription() {
  Close().IgnoreError();
}
absl::StatusOr<JoshuaWireCoordinator::Observation> JoshuaWireCoordinator::Subscription::Latest()
    const {
  if (closed_) return absl::FailedPreconditionError("Subscription handle closed");
  std::lock_guard lock(owner_->impl_->mutex);
  if (owner_->impl_->closing || state_->retired || !state_->active)
    return absl::UnavailableError("Sensor subscription generation unavailable");
  if (!state_->latest) return absl::UnavailableError("Sensor has no acquisition yet");
  auto result = *state_->latest;
  result.receipt_age =
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - result.received_at);
  result.receipt_fresh = result.receipt_age <= std::chrono::microseconds(result.schema.max_age_us);
  result.usable = result.receipt_fresh && result.sample.sample.metadata.validity == JW_SAMPLE_VALID;
  return result;
}
absl::Status JoshuaWireCoordinator::Subscription::Close() {
  if (closed_.exchange(true)) return absl::OkStatus();
  auto impl = owner_->impl_;
  {
    std::lock_guard lock(impl->mutex);
    if (state_->consumers) --state_->consumers;
    if (state_->consumers || state_->retired || impl->closing) return absl::OkStatus();
    state_->retired = true;
    state_->active = false;
    state_->latest.reset();
    --impl->diagnostics.active_streams;
    ++impl->diagnostics.retired_streams;
  }
  const auto deadline = Clock::now() + Ms(impl->policy.operation_timeout_ms);
  std::unique_lock management(impl->subscription_mutex, std::defer_lock);
  if (!management.try_lock_until(deadline))
    return absl::DeadlineExceededError("Unsubscribe deferred; disabled reconnect required");
  uint8_t token[JW_STREAM_TOKEN_LEN];
  jw_encode_stream_token(token, sizeof(token), state_->token);
  const auto payload = StreamRequest(JW_SENSOR_UNSUBSCRIBE, token, sizeof(token));
  auto operation =
      owner_
          ->Submit({0, 0, JW_CMD_PROFILE, state_->function, payload.data(), payload.size()},
                   deadline)
          .Wait();
  auto data = ReplyData(operation);
  if (!data.ok()) return data.status();
  jw_stream_state_t state{};
  if (jw_decode_stream_state(data->data(), data->size(), &state) != JW_RESULT_OK || state.active ||
      state.subscription_id != state_->token || state.period_us != state_->period)
    return absl::DataLossError("Unsubscribe state mismatch; disabled reconnect required");
  {
    std::lock_guard lock(impl->mutex);
    impl->streams.erase(state_->function);
    --impl->diagnostics.retired_streams;
  }
  return absl::OkStatus();
}
absl::StatusOr<jw_stream_state_t> JoshuaWireCoordinator::QuerySubscription(uint8_t function) {
  if (function == JW_CHANNEL_NONE)
    return absl::InvalidArgumentError("Subscription query requires a sensor function");
  auto payload = StreamRequest(JW_SENSOR_GET_SUBSCRIPTION);
  auto result = Submit({0, 0, JW_CMD_PROFILE, function, payload.data(), payload.size()}).Wait();
  ABSL_ASSIGN_OR_RETURN(auto data, ReplyData(result));
  jw_stream_state_t state{};
  if (jw_decode_stream_state(data.data(), data.size(), &state) != JW_RESULT_OK)
    return absl::DataLossError("Malformed subscription state");
  return state;
}
absl::StatusOr<std::shared_ptr<JoshuaWireCoordinator>> JoshuaWireCoordinator::Reconnect(
    Reopen reopen, Reconcile reconcile, std::function<uint32_t()> source) {
  if (!reopen || !reconcile)
    return absl::InvalidArgumentError(
        "Reconnect requires endpoint selection and disabled identity reconciliation");
  const auto prior = Snapshot().session_id;
  const auto policy = impl_->policy;
  Close().IgnoreError();
  uint32_t closed_session;
  {
    std::lock_guard lock(impl_->mutex);
    closed_session = impl_->last_attempt_session;
  }
  if (!source) source = JoshuaWireSession::AllocateSessionId;
  auto fresh = [source = std::move(source), prior, closed_session]() mutable {
    for (int i = 0; i < 16; ++i) {
      const auto value = source();
      if (value && value != prior && value != closed_session) return value;
    }
    return uint32_t(0);
  };
  ABSL_ASSIGN_OR_RETURN(auto link, reopen());
  // Reconstruct the effective protobuf policy, avoiding a second defaults table.
  JoshuaWireHostPolicy config;
#define JW_SET_POLICY(field) config.set_##field(policy.field)
  JW_SET_POLICY(operation_timeout_ms);
  JW_SET_POLICY(attempt_timeout_ms);
  JW_SET_POLICY(polling_timeout_ms);
  JW_SET_POLICY(max_attempts);
  JW_SET_POLICY(max_pending_operations);
  JW_SET_POLICY(max_pending_bytes);
  JW_SET_POLICY(max_priority_burst);
  JW_SET_POLICY(scheduling_guard_ms);
  JW_SET_POLICY(max_notifications);
  JW_SET_POLICY(max_streams);
  JW_SET_POLICY(telemetry_bytes_per_second);
#undef JW_SET_POLICY
  ABSL_ASSIGN_OR_RETURN(auto next, Acquire(std::move(link), config, std::move(fresh)));
  {
    std::lock_guard lock(next->impl_->mutex);
    next->impl_->diagnostics.safety_interlock = true;
  }
  absl::Status status;
  try {
    status = reconcile(*next);
  } catch (...) {
    status = absl::InternalError("Reconnect identity reconciliation threw");
  }
  auto snapshot = next->Snapshot();
  if (!status.ok() || !snapshot.ready || snapshot.faulted || snapshot.uncertain ||
      snapshot.closed) {
    next->Close().IgnoreError();
    return status.ok() ? absl::FailedPreconditionError(
                             "Reconnect reconciliation did not establish a clean disabled session")
                       : status;
  }
  {
    std::lock_guard lock(next->impl_->mutex);
    next->impl_->diagnostics.safety_interlock = false;
  }
  return next;
}

}  // namespace robot::board
