#pragma once

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>

#include "robot/perception/interfaces/perception_interface.h"

namespace robot::perception {

inline std::string ObservationHostClockId() {
  static const std::string id = [] {
    std::ifstream file("/proc/sys/kernel/random/boot_id");
    std::string boot;
    std::getline(file, boot);
    return boot.empty() ? "unavailable" : "linux-monotonic:" + boot;
  }();
  return id;
}
inline std::string NewObservationInstance() {
  static std::atomic<uint64_t> sequence{0};
  return ObservationHostClockId() + "/" + std::to_string(getpid()) + "/" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "/" +
         std::to_string(++sequence);
}
struct ObservationDescription {
  std::string native_unit;
  std::string frame_id;
};

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
  ObservationState(std::chrono::milliseconds stale_after,
                   Clock::time_point start = Clock::now(),
                   ObservationDescription description = {})
      : stale_after_(stale_after), description_(std::move(description)), started_(start) {
    health_.set_source_instance_id(instance_id_);
    health_.set_host_clock_id(ObservationHostClockId());
  }
  void Record(absl::StatusOr<PerceptionPacket> result,
              Clock::time_point now,
              int64_t unix_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::system_clock::now().time_since_epoch())
                                    .count()) {
    std::lock_guard lock(mutex_);
    if (health_.state() == ObservationStatus::STOPPED) return;
    auto status = result.status();
    if (result.ok()) status = ValidateObservation(*result);
    if (!status.ok()) {
      pending_discontinuity_ = true;
      health_.set_state(ObservationStatus::ERROR);
      health_.set_errors(health_.errors() + 1);
      health_.set_detail(status.ToString().substr(0, 512));
      health_.set_last_error(health_.detail());
      health_.set_last_error_steady_ns(
          std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
      return;
    }
    bool distinct = true;
    bool source_changed = false;
    if (result->has_scalar()) {
      const auto& s = result->scalar();
      const auto identity = std::make_tuple(
          s.boot_id(), s.session_id(), s.subscription_id(), s.acquisition_sequence());
      distinct = !have_scalar_ || identity != scalar_identity_;
      source_changed = !have_scalar_ || std::get<0>(identity) != std::get<0>(scalar_identity_) ||
                       std::get<1>(identity) != std::get<1>(scalar_identity_) ||
                       std::get<2>(identity) != std::get<2>(scalar_identity_);
      have_scalar_ = true;
      scalar_identity_ = identity;
    }
    const bool usable =
        !result->has_scalar() || (result->scalar().usable() && result->scalar().receipt_fresh() &&
                                  std::isfinite(result->scalar().value()));
    if (distinct) {
      if (usable) {
        if (health_.state() == ObservationStatus::ERROR ||
            health_.state() == ObservationStatus::INVALID ||
            (have_valid_ && now - last_valid_ >= stale_after_)) {
          health_.set_recoveries(health_.recoveries() + 1);
          health_.set_last_recovery_steady_ns(
              std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
        }
        last_valid_ = now;
        have_valid_ = true;
        health_.set_valid_samples(health_.valid_samples() + 1);
      } else
        health_.set_invalid_samples(health_.invalid_samples() + 1);
    }
    if (distinct || !packet_) {
      auto* metadata = result->mutable_sample_metadata();
      metadata->Clear();
      metadata->set_source_instance_id(instance_id_);
      metadata->set_sequence(++sample_sequence_);
      metadata->set_discontinuity(pending_discontinuity_ || source_changed);
      metadata->set_host_receipt_unix_ns(unix_ns);
      metadata->set_host_receipt_steady_ns(
          std::chrono::duration_cast<std::chrono::nanoseconds>(now.time_since_epoch()).count());
      metadata->set_host_clock_id(ObservationHostClockId());
      metadata->set_native_unit(description_.native_unit);
      metadata->set_frame_id(description_.frame_id);
      metadata->set_measurement_valid(usable);
      if (result->has_scalar()) {
        const auto& scalar = result->scalar();
        if (scalar.boot_id() && scalar.clock()) {
          metadata->set_acquisition_ticks(scalar.acquisition_us());
          metadata->set_acquisition_tick_ns(1000);
          metadata->set_acquisition_clock_id("jw-device:" + std::to_string(scalar.boot_id()) + ":" +
                                             std::to_string(scalar.clock()));
        }
        metadata->set_source_sequence(scalar.acquisition_sequence());
        metadata->set_native_unit("jw-unit:" + std::to_string(scalar.unit()));
      }
      if (result->has_point_cloud() && metadata->frame_id().empty())
        metadata->set_frame_id(result->point_cloud().frame_id());
      pending_discontinuity_ = false;
      result->set_sequence_id(sample_sequence_);
      // Preserve the driver's RobotTime acquisition estimate; receipt has its own clock.
    } else {
      // Cached publications retain original identity and receipt times.
      *result->mutable_sample_metadata() = packet_->sample_metadata();
      result->set_sequence_id(packet_->sequence_id());
      result->set_timestamp(packet_->timestamp());
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
  const std::string instance_id_ = NewObservationInstance();
  const std::chrono::milliseconds stale_after_;
  const ObservationDescription description_;
  uint64_t sample_sequence_ = 0;
  bool pending_discontinuity_ = true;
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
                    std::chrono::milliseconds stale_after,
                    ObservationDescription description = {})
      : state_(stale_after, ObservationState::Clock::now(), std::move(description)),
        worker_([this, factory, period] { Run(factory, period); }) {}
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
