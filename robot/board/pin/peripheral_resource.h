#pragma once
#include <string>
#include <vector>

#include "robot/board/pin/pin_resource.h"
namespace robot::board {
struct PeripheralClaim {
  std::string identity;
  std::string sharing;  // Empty means exclusive; matching roles/settings may share a bus.
};
struct PeripheralResource {
  BoardPeripheral config;
  std::vector<PinInfo> pins;  // PWM; SPI clock/MOSI/MISO/CS; I2C SDA/SCL; UART TX/RX.
  std::vector<PeripheralClaim> claims;
};
// Includes cross-operation GPIO/actuator pin checks. No device acquisition.
absl::StatusOr<std::vector<PeripheralResource>> ResolveBoardPeripherals(const Board& board);
std::string PinIdentity(const PinProfile& profile, const PinInfo& pin);
}  // namespace robot::board
