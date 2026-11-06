#pragma once

#include <chrono>
#include <cmath>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>

#include "robot/perception/interfaces/perception_interface.h"

namespace robot::perception {

// Shared validation at the acquisition boundary, before readiness is granted.
inline absl::Status ValidateObservation(const PerceptionPacket& p) {
  if (p.ByteSizeLong() > 64 * 1024 * 1024)
    return absl::ResourceExhaustedError("Observation exceeds 64 MiB");
  if (p.has_scalar()) return absl::OkStatus();  // Invalid scalar measurements stay inspectable.
  if (p.has_position() && std::isfinite(p.position().position())) return absl::OkStatus();
  if (p.has_image()) {
    const auto& i = p.image();
    if (i.width() > 0 && i.height() > 0 && i.channels() == 3 && i.encoding() == "bgr8" &&
        uint64_t(i.width()) * i.height() * 3 == i.data().size())
      return absl::OkStatus();
  }
  if (p.has_point_cloud()) {
    const auto& c = p.point_cloud();
    if (c.x_size() > 0 && c.y_size() == c.x_size() && c.z_size() == c.x_size() &&
        (c.intensity_size() == 0 || c.intensity_size() == c.x_size()) &&
        (c.time_offset_size() == 0 || c.time_offset_size() == c.x_size()))
      return absl::OkStatus();
  }
  return absl::DataLossError("Invalid or unsupported observation payload");
}

// Latest-only storage, controlled-clock readiness and bounded diagnostics. No ROS,
// transport, device commands, or invented acquisition timestamp is involved.
class ObservationState {
 public:
  using Clock = std::chrono::steady_clock;
  struct Snapshot {
    ObservationStatus health;
    std::shared_ptr<const PerceptionPacket> packet;
  };
  ObservationState(std::chrono::milliseconds stale_after, Clock::time_point start = Clock::now())
      : stale_after_(stale_after), started_(start) {}
  void Record(absl::StatusOr<PerceptionPacket> result, Clock::time_point now) {
    std::lock_guard lock(mutex_);
    if (health_.state() == ObservationStatus::STOPPED) return;
    auto status = result.status();
    if (result.ok()) status = ValidateObservation(*result);
    if (!status.ok()) {
      health_.set_state(ObservationStatus::ERROR);
      health_.set_errors(health_.errors() + 1);
      health_.set_detail(status.ToString().substr(0, 512));
      return;
    }
    bool distinct = true;
    if (result->has_scalar()) {
      const auto& s = result->scalar();
      const auto identity = std::make_tuple(
          s.boot_id(), s.session_id(), s.subscription_id(), s.acquisition_sequence());
      distinct = !have_scalar_ || identity != scalar_identity_;
      have_scalar_ = true;
      scalar_identity_ = identity;
    }
    const bool usable =
        !result->has_scalar() || (result->scalar().usable() && result->scalar().receipt_fresh() &&
                                  std::isfinite(result->scalar().value()));
    if (distinct) {
      if (usable) {
        last_valid_ = now;
        have_valid_ = true;
        health_.set_valid_samples(health_.valid_samples() + 1);
      } else
        health_.set_invalid_samples(health_.invalid_samples() + 1);
    }
    health_.set_state(usable ? ObservationStatus::READY : ObservationStatus::INVALID);
    health_.clear_detail();
    packet_ = std::make_shared<const PerceptionPacket>(std::move(*result));
  }
  Snapshot Get(Clock::time_point now = Clock::now()) const {
    std::lock_guard lock(mutex_);
    Snapshot result{health_, packet_};
    if (have_valid_)
      result.health.set_last_valid_age_ns(
          std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_valid_).count());
    const auto since = have_valid_ ? last_valid_ : started_;
    if ((result.health.state() == ObservationStatus::READY ||
         result.health.state() == ObservationStatus::STARTING) &&
        now - since >= stale_after_)
      result.health.set_state(ObservationStatus::STALE);
    return result;
  }
  void Stop() {
    std::lock_guard lock(mutex_);
    health_.set_state(ObservationStatus::STOPPED);
    packet_.reset();
  }

 private:
  const std::chrono::milliseconds stale_after_;
  Clock::time_point started_, last_valid_{};
  bool have_valid_ = false, have_scalar_ = false;
  std::tuple<uint32_t, uint32_t, uint32_t, uint32_t> scalar_identity_{};
  mutable std::mutex mutex_;
  ObservationStatus health_;
  std::shared_ptr<const PerceptionPacket> packet_;
};

// One acquisition owner per configured sensor. Slow reads never run on the ROS
// executor; all output topics fan out from the same bounded latest sample.
class ObservationSource {
 public:
  using Factory = std::function<absl::StatusOr<std::unique_ptr<PerceptionInterface>>()>;
  ObservationSource(Factory factory,
                    std::chrono::nanoseconds period,
                    std::chrono::milliseconds stale_after)
      : state_(stale_after), worker_([this, factory, period] { Run(factory, period); }) {}
  ~ObservationSource() {
    Stop();
  }
  ObservationState::Snapshot Get() const {
    return state_.Get();
  }
  void Stop() {
    std::lock_guard close_lock(close_mutex_);
    std::shared_ptr<PerceptionInterface> source;
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      source = source_;
    }
    wake_.notify_all();
    if (source) std::call_once(teardown_, [source] { source->Teardown().IgnoreError(); });
    if (worker_.joinable()) worker_.join();
    state_.Stop();
  }

 private:
  void Run(const Factory& factory, std::chrono::nanoseconds period) {
    try {
      auto made = factory();
      if (!made.ok()) {
        state_.Record(made.status(), ObservationState::Clock::now());
        return;
      }
      auto source = std::shared_ptr<PerceptionInterface>(std::move(*made));
      {
        std::lock_guard lock(mutex_);
        source_ = source;
      }
      for (;;) {
        {
          std::lock_guard lock(mutex_);
          if (stopping_) break;
        }
        const auto start = ObservationState::Clock::now();
        auto packet = source->GetData();
        {
          std::lock_guard lock(mutex_);
          if (stopping_) break;
        }
        state_.Record(std::move(packet), ObservationState::Clock::now());
        std::unique_lock lock(mutex_);
        wake_.wait_until(lock, start + period, [this] { return stopping_; });
        if (stopping_) break;
      }
      std::call_once(teardown_, [source] { source->Teardown().IgnoreError(); });
    } catch (const std::exception& e) {
      state_.Record(absl::InternalError(e.what()), ObservationState::Clock::now());
    }
  }
  ObservationState state_;
  mutable std::mutex mutex_;
  std::condition_variable wake_;
  bool stopping_ = false;
  std::shared_ptr<PerceptionInterface> source_;
  std::mutex close_mutex_;
  std::once_flag teardown_;
  std::thread worker_;
};
}  // namespace robot::perception
