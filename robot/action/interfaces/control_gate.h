#pragma once

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <string>

#include "robot/action/interfaces/action_interface.h"
#include "robot/action/proto/action.pb.h"

namespace robot::action {

// Transport/ROS-independent admission and lifecycle. Calls are serialized across
// all subscriptions of one actuator; no command or enable is ever retried.
class ControlGate {
 public:
  using Clock = std::chrono::steady_clock;
  enum class State { kDisabled, kEnabled, kStopped, kFault, kClosed };
  ControlGate(std::shared_ptr<ActionInterface> action,
              const ControlPolicy& policy,
              std::set<std::string> sources)
      : action_(std::move(action)),
        sources_(std::move(sources)),
        source_(policy.initial_source().empty() ? "default" : policy.initial_source()),
        timeout_(policy.has_command_timeout_ms() ? policy.command_timeout_ms() : 1000),
        max_age_ns_(int64_t(policy.max_input_age_ms()) * 1000000) {}
  ~ControlGate() {
    Shutdown().IgnoreError();
  }

  absl::Status Initialize() {
    std::lock_guard lock(mutex_);
    if (!sources_.count(source_) || timeout_.count() <= 0 || timeout_.count() > INT32_MAX)
      return absl::InvalidArgumentError("Invalid control policy/source");
    return DisableLocked(State::kDisabled);
  }
  absl::Status Enable(Clock::time_point now, int64_t ros_now_ns = 0) {
    std::string selected;
    {
      std::lock_guard lock(mutex_);
      selected = source_;
    }
    ActionPacket packet;
    packet.set_preset(PRESET_ENABLE_TORQUE);
    return Execute(selected, packet, ros_now_ns, now);
  }
  absl::Status Execute(const std::string& source,
                       const ActionPacket& packet,
                       int64_t ros_now_ns,
                       Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (state_ == State::kClosed) return absl::UnavailableError("Actuator is closed");
    if (!sources_.count(source)) return absl::PermissionDeniedError("Unknown command source");
    // Any declared producer may disable; it cannot clear a latched stop/fault.
    if (packet.has_preset() && packet.preset() == PRESET_DISABLE_TORQUE)
      return DisableLocked(state_ == State::kEnabled ? State::kDisabled : state_);
    if (source != source_) return absl::PermissionDeniedError("Inactive command source");
    if (state_ == State::kStopped || state_ == State::kFault)
      return absl::FailedPreconditionError("Explicit disabled recovery is required");
    if (state_ == State::kEnabled && now >= deadline_) {
      DisableLocked(State::kStopped).IgnoreError();
      return absl::DeadlineExceededError("Command lease expired");
    }
    if (packet.has_preset() && packet.preset() == PRESET_ENABLE_TORQUE) {
      if (state_ == State::kEnabled)
        return absl::OkStatus();  // Enable cannot renew a command lease.
      auto status = action_->SetAction(packet);
      if (!status.ok()) return FaultLocked(status);
      source_epoch_ns_ = std::max(source_epoch_ns_, ros_now_ns);
      last_stamp_ns_ = 0;
      state_ = State::kEnabled;
      deadline_ = now + timeout_;
      return status;
    }
    if (!packet.has_joint()) return absl::InvalidArgumentError("Expected motion or enable/disable");
    if (state_ != State::kEnabled) return absl::FailedPreconditionError("Actuator is disabled");
    if (now >= deadline_) {
      DisableLocked(State::kStopped).IgnoreError();
      return absl::DeadlineExceededError("Command lease expired");
    }
    if (max_age_ns_) {
      const auto stamp = static_cast<int64_t>(packet.timestamp_ns());
      if (stamp <= 0 || ros_now_ns < stamp || ros_now_ns - stamp > max_age_ns_ ||
          stamp < source_epoch_ns_ || stamp <= last_stamp_ns_)
        return absl::DeadlineExceededError("Stale, future, duplicate or prior-source input");
    }
    auto status = action_->SetAction(packet);
    if (!status.ok()) return FaultLocked(status);
    if (max_age_ns_) last_stamp_ns_ = static_cast<int64_t>(packet.timestamp_ns());
    deadline_ = now + timeout_;
    return status;
  }
  absl::Status Poll(Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (state_ == State::kEnabled && now >= deadline_) return DisableLocked(State::kStopped);
    return absl::OkStatus();
  }
  absl::Status Stop() {
    std::lock_guard lock(mutex_);
    if (state_ == State::kClosed) return absl::UnavailableError("Actuator is closed");
    return DisableLocked(State::kStopped);
  }
  absl::Status Recover(int64_t ros_now_ns) {
    std::lock_guard lock(mutex_);
    if (state_ == State::kEnabled || state_ == State::kClosed)
      return absl::FailedPreconditionError("Disable/stop before recovery");
    source_epoch_ns_ = ros_now_ns;
    last_stamp_ns_ = 0;
    return DisableLocked(State::kDisabled);
  }
  absl::Status SelectSource(const std::string& source, int64_t ros_now_ns) {
    std::lock_guard lock(mutex_);
    if (state_ != State::kDisabled)
      return absl::FailedPreconditionError("Select source only while disabled");
    if (!sources_.count(source)) return absl::InvalidArgumentError("Unknown source");
    source_ = source;
    source_epoch_ns_ = ros_now_ns;
    last_stamp_ns_ = 0;
    return absl::OkStatus();
  }
  absl::Status Shutdown() {
    std::lock_guard lock(mutex_);
    if (state_ == State::kClosed) return absl::OkStatus();
    auto status = DisableLocked(State::kClosed);
    state_ = State::kClosed;  // Retained callbacks can never revive this generation.
    return status;
  }
  State state() const {
    std::lock_guard lock(mutex_);
    return state_;
  }

 private:
  absl::Status DisableLocked(State next) {
    state_ = next;  // Interlock before issuing the possibly failing output operation.
    ActionPacket packet;
    packet.set_preset(PRESET_DISABLE_TORQUE);
    auto status = action_->SetAction(packet);
    if (!status.ok()) state_ = State::kFault;
    return status;
  }
  absl::Status FaultLocked(absl::Status cause) {
    DisableLocked(State::kFault).IgnoreError();
    return cause;
  }
  std::shared_ptr<ActionInterface> action_;
  const std::set<std::string> sources_;
  std::string source_;
  std::chrono::milliseconds timeout_;
  int64_t max_age_ns_, source_epoch_ns_ = 0, last_stamp_ns_ = 0;
  Clock::time_point deadline_{};
  mutable std::mutex mutex_;
  State state_ = State::kDisabled;
};
}  // namespace robot::action
