#include "robot/board/joshua_wire/joshua_wire_coordinator.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>

#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "firmware/common/joshua_wire_sensor_stream.h"
#include "robot/board/joshua_wire/joshua_wire_session.h"

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
  Result result;
};

struct JoshuaWireCoordinator::Impl {
  using Work = std::shared_ptr<Operation::State>;
  std::shared_ptr<Link> link;
  const CoordinatorPolicy policy;
  JoshuaWireSession session;
  mutable std::mutex mutex;
  std::mutex close_mutex;
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

  bool OnWorker() const {
    return running && std::this_thread::get_id() == worker_id;
  }
  void Complete(const Work& work, absl::Status status, bool counted = true) {
    if (counted) {
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
    std::lock_guard lock(mutex);
    if (failure.ok()) failure = std::move(status);
    diagnostics.faulted = true;
    diagnostics.ready = false;
    diagnostics.last_error = failure.ToString().substr(0, 512);
    closing = true;
  }
  void RouteNotification(Link::ReceivedFrame frame) {
    jw_frame_t decoded{};
    if (jw_decode_frame(frame.bytes.data(), frame.bytes.size(), &decoded) != JW_RESULT_OK) return;
    std::lock_guard lock(mutex);
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
    notifications.push_back(std::move(frame));
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
      for (;;) {
        ExpireQueued();
        Work work;
        {
          std::lock_guard lock(mutex);
          if (closing) break;
          work = SelectLocked();
          active = work;
        }
        if (!work) {
          auto frame = link->Receive(Clock::time_point::max());
          if (!frame.ok()) {
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
          }
        }
        UpdateSession();
        {
          std::lock_guard lock(mutex);
          active.reset();
        }
        Complete(work, result);
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
  };
  static auto* cache = new std::map<Link*, Entry>;
  std::lock_guard lock(*mutex);
  for (auto it = cache->begin(); it != cache->end();) {
    if (it->second.owner.expired() && it->second.reservation.expired())
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
  (*cache)[link.get()] = {owner, state};
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
JoshuaWireCoordinator::Operation JoshuaWireCoordinator::Submit(const jw_command_t& command) {
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
  work->deadline = admitted + Ms(budget);
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
    if (impl_->active && impl_->active->priority != 0) {
      impl_->active->cancel = true;
      ++impl_->diagnostics.retired;
    }
  } else {
    ++impl_->diagnostics.pending_operations;
    impl_->diagnostics.pending_bytes += work->reservation;
  }
  lock.unlock();
  impl_->link->Interrupt();
  return Operation(work);
}
absl::StatusOr<Bytes> JoshuaWireCoordinator::Exchange(const jw_command_t& command) {
  auto result = Submit(command).Wait();
  if (!result.status.ok()) return result.status;
  return std::move(result.payload);
}
void JoshuaWireCoordinator::Operation::Cancel() const {
  {
    std::lock_guard lock(state_->mutex);
    if (state_->done) return;
  }
  state_->cancel = true;
  if (auto owner = state_->owner.lock()) owner->link->Interrupt();
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
    if (impl_->active) impl_->active->cancel = true;
  }
  impl_->link->Interrupt();
  if (impl_->worker.joinable()) impl_->worker.join();
  return impl_->close_status;
}
}  // namespace robot::board
