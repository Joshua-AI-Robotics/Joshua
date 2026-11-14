#pragma once
#include "absl/status/status.h"
#include "robot/comm/proto/comm.pb.h"
namespace robot::comm {
inline absl::Status ValidateSpiSettings(const SpiConfig& c, bool mcu = false) {
  if ((mcu && c.speed_hz() < 100000) || !c.speed_hz() || c.speed_hz() > 50000000 || c.mode() > 3 ||
      (c.bits_per_word() != 0 && c.bits_per_word() != 8))
    return absl::InvalidArgumentError(
        "SPI requires speed 1..50000000 Hz, mode 0..3 and 8-bit words");
  if ((!mcu && (c.device().empty() || c.device().front() != '/' || c.controller().empty())) ||
      (mcu && (!c.device().empty() || !c.controller().empty())))
    return absl::InvalidArgumentError(
        "SPI requires Linux device/controller selectors or MCU-native routing");
  if (mcu ? c.has_chip_select_index() : (!c.has_chip_select_index() || c.chip_select_index() > 1))
    return absl::InvalidArgumentError(
        "SPI host requires chip_select_index 0/1; MCU selects its configured GPIO");
  return absl::OkStatus();
}
inline absl::Status ValidateI2cSettings(const I2cConfig& c, bool mcu = false) {
  if (c.address() < 8 || c.address() > 0x77 || c.timeout_ms() < 1 ||
      c.timeout_ms() > (mcu ? 5u : 100u) ||
      (mcu ? (c.frequency_hz() < 10000 || c.frequency_hz() > 400000) : c.frequency_hz() != 0))
    return absl::InvalidArgumentError(
        "I2C requires unreserved 7-bit address, timeout 1..100 ms; MCU clock 10..400 kHz, Linux "
        "clock from device tree");
  if ((!mcu && (c.device().empty() || c.device().front() != '/' || c.controller().empty())) ||
      (mcu && (!c.device().empty() || !c.controller().empty())))
    return absl::InvalidArgumentError(
        "I2C requires Linux device/controller selectors or MCU-native routing");
  return absl::OkStatus();
}
}  // namespace robot::comm
