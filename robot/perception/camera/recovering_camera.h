#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <utility>

#include "robot/perception/interfaces/perception_interface.h"

namespace robot::perception {

// Camera-only recovery, called by the shared acquisition worker. Board sessions
// and subscriptions must never inherit automatic reopen behavior.
class RecoveringCamera : public PerceptionInterface {
 public:
  using Now = std::function<double()>;
  explicit RecoveringCamera(std::unique_ptr<PerceptionInterface> camera,
                            Now now = joshua::RobotTime)
      : camera_(std::move(camera)), now_(std::move(now)), id_(camera_->GetId()) {}

  absl::Status Init() override {
    std::lock_guard lock(mutex_);
    return InitializeLocked();
  }
  std::string GetId() override {
    return id_;
  }

  absl::StatusOr<PerceptionPacket> GetData() override {
    std::lock_guard lock(mutex_);
    if (stopped_) return absl::CancelledError("Camera stopped");
    if (!initialized_) {
      if (absl::IsInvalidArgument(last_error_) || now_() < retry_after_) return last_error_;
      const auto status = InitializeLocked();
      if (!status.ok()) return status;
    }
    auto packet = camera_->GetData();
    if (!packet.ok()) ResetLocked(packet.status());
    return packet;
  }

  absl::Status Teardown() override {
    std::lock_guard lock(mutex_);
    stopped_ = true;
    initialized_ = false;
    if (!active_) return absl::OkStatus();
    active_ = false;
    return camera_->Teardown();
  }

 private:
  absl::Status InitializeLocked() {
    if (stopped_) return absl::CancelledError("Camera stopped");
    if (initialized_) return absl::OkStatus();
    active_ = true;
    const auto status = camera_->Init();
    if (status.ok())
      initialized_ = true;
    else
      ResetLocked(status);
    return status;
  }
  void ResetLocked(const absl::Status& status) {
    initialized_ = false;
    last_error_ = status;
    if (active_) {
      active_ = false;
      const auto cleanup = camera_->Teardown();
      if (!cleanup.ok())
        last_error_ = absl::Status(
            status.code(), std::string(status.message()) + "; teardown: " + cleanup.ToString());
    }
    retry_after_ = now_() + 1.0;
  }

  std::unique_ptr<PerceptionInterface> camera_;
  Now now_;
  const std::string id_;
  std::mutex mutex_;
  bool initialized_ = false, active_ = false, stopped_ = false;
  double retry_after_ = 0.0;
  absl::Status last_error_ = absl::UnavailableError("Camera not initialized");
};
}  // namespace robot::perception
