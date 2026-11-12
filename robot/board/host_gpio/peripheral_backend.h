#pragma once
#include <memory>
#include <string>
#include <vector>

#include "robot/board/interfaces/board_interface.h"
#include "robot/board/pin/peripheral_resource.h"
namespace robot::board {
class PeripheralBackend {
 public:
  virtual ~PeripheralBackend() = default;
  virtual absl::StatusOr<std::shared_ptr<PwmOutput>> Pwm(const PeripheralResource&) = 0;
  virtual absl::StatusOr<std::shared_ptr<robot::comm::SpiTransport>> Spi(
      const PeripheralResource&) = 0;
  virtual absl::StatusOr<std::shared_ptr<robot::comm::I2cTransport>> I2c(
      const PeripheralResource&) = 0;
  virtual absl::StatusOr<std::shared_ptr<robot::comm::ByteStream>> Uart(
      const PeripheralResource&) = 0;
};
class PeripheralSystem {
 public:
  virtual ~PeripheralSystem() = default;
  virtual std::vector<std::string> PwmChips() = 0;
  virtual absl::StatusOr<std::string> Read(const std::string& path) = 0;
  virtual absl::Status Write(const std::string& path, const std::string& value) = 0;
  virtual bool Exists(const std::string& path) = 0;
  virtual absl::StatusOr<std::string> Controller(const std::string& path,
                                                 bool character_device) = 0;
};
std::shared_ptr<PeripheralBackend> MakeLinuxPeripheralBackend(
    std::shared_ptr<PeripheralSystem> system = {});
}  // namespace robot::board
