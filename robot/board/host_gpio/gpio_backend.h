#pragma once

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/board/pin/pin_resource.h"

namespace robot::board {

class GpioLine {
 public:
  virtual ~GpioLine() = default;  // Releases exclusive kernel request.
  virtual absl::StatusOr<bool> Read() = 0;
  virtual absl::Status Write(bool active) = 0;
};

class GpioBackend {
 public:
  virtual ~GpioBackend() = default;
  virtual absl::Status CheckPlatform(const PinProfile& profile) = 0;
  // Atomically acquire direction/polarity/bias and the initial logical value.
  virtual absl::StatusOr<std::unique_ptr<GpioLine>> Request(const PinResource& pin) = 0;
};

}  // namespace robot::board
