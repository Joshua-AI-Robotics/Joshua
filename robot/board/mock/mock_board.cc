#include "robot/board/mock/mock_board.h"

#include <string>

#include "absl/strings/str_cat.h"

namespace robot::board {

namespace {

class MockBoardChannel : public BoardChannel {
 public:
  explicit MockBoardChannel(uint32_t index) : index_(index) {}

  absl::Status Enable() override {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = true;
    return absl::OkStatus();
  }

  absl::Status Disable() override {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = false;
    return absl::OkStatus();
  }

  absl::Status SetTarget(TargetMode mode, float value) override {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!enabled_) {
      return absl::FailedPreconditionError(
          absl::StrCat("Mock channel ", index_, " is not enabled."));
    }
    switch (mode) {
      case TargetMode::kPosition:
        feedback_.position = value;
        break;
      case TargetMode::kVelocity:
        feedback_.velocity = value;
        break;
      case TargetMode::kTorque:
        return absl::UnimplementedError(
            absl::StrCat("Mock channel ", index_, " does not support torque targets."));
    }
    return absl::OkStatus();
  }

  absl::StatusOr<ChannelFeedback> ReadFeedback() override {
    std::lock_guard<std::mutex> lock(mutex_);
    return feedback_;
  }

 private:
  const uint32_t index_;
  std::mutex mutex_;
  bool enabled_ = false;
  ChannelFeedback feedback_;
};

}  // namespace

absl::Status MockBoard::Init(const robot::board::Board& config) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (initialized_) {
    return absl::FailedPreconditionError(
        absl::StrCat("Mock board '", config.name(), "' is already initialized."));
  }
  config_ = config;
  for (const auto& channel : config.channels()) {
    if (channels_.count(channel.index()) > 0) {
      return absl::InvalidArgumentError(absl::StrCat("Board '",
                                                     config.name(),
                                                     "' declares channel index ",
                                                     channel.index(),
                                                     " more than once."));
    }
    channels_[channel.index()] = std::make_shared<MockBoardChannel>(channel.index());
  }
  initialized_ = true;
  return absl::OkStatus();
}

absl::StatusOr<std::shared_ptr<BoardChannel>> MockBoard::OpenChannel(uint32_t index) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    return absl::FailedPreconditionError("Mock board is not initialized.");
  }
  auto it = channels_.find(index);
  if (it == channels_.end()) {
    return absl::NotFoundError(absl::StrCat("Board '",
                                            config_.name(),
                                            "' has no channel ",
                                            index,
                                            "; declare it in the board's channels{}."));
  }
  return it->second;
}

absl::Status MockBoard::Teardown() {
  std::lock_guard<std::mutex> lock(mutex_);
  channels_.clear();
  initialized_ = false;
  return absl::OkStatus();
}

}  // namespace robot::board
