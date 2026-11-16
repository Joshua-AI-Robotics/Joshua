#pragma once

#include <memory>
#include <string>
#include <vector>

#include "robot/board/host_gpio/gpio_backend.h"

namespace robot::board {

// Syscall seam for tests of discovery, ABI flags, ownership and descriptor cleanup.
// Implementations use errno on failure, just like the corresponding Linux calls.
class GpioSystem {
 public:
  virtual ~GpioSystem() = default;
  virtual std::vector<std::string> Chips() = 0;
  virtual std::string Compatible() = 0;  // NUL-separated device-tree tokens.
  virtual int Open(const std::string& path) = 0;
  virtual int Ioctl(int fd, unsigned long operation, void* argument) = 0;
  virtual void Close(int fd) = 0;
};

// Linux GPIO character-device ABI v2 (kernel >= 5.10). No sysfs/MMIO fallback.
class LinuxGpioBackend final : public GpioBackend {
 public:
  LinuxGpioBackend();
  explicit LinuxGpioBackend(std::shared_ptr<GpioSystem> system);
  absl::Status CheckPlatform(const PinProfile& profile) override;
  absl::StatusOr<std::unique_ptr<GpioLine>> Request(const PinResource& pin) override;

 private:
  std::shared_ptr<GpioSystem> system_;
};

}  // namespace robot::board
