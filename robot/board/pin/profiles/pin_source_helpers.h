#pragma once

#include <initializer_list>
#include <optional>
#include <utility>

#include "robot/board/pin/pin_source.h"

namespace robot::board::profiles {

inline PinInfo Power(uint32_t n, uint32_t voltage_mv) {
  return {n,
          voltage_mv == 3300 ? "3V3" : "5V",
          PinKind::kPower,
          voltage_mv,
          {},
          {},
          {},
          0,
          std::nullopt,
          {},
          "Power supply; never a GPIO"};
}
inline PinInfo Ground(uint32_t n) {
  return {n, "GND", PinKind::kGround, 0, {}, {}, {}, 0, std::nullopt, {}, "Ground; never a GPIO"};
}
inline PinInfo Peripheral(uint32_t n,
                          std::string signal,
                          PinFunction function,
                          bool reserved = false) {
  return {n,
          std::move(signal),
          reserved ? PinKind::kReserved : PinKind::kPeripheral,
          3300,
          {function},
          {},
          {},
          0,
          std::nullopt,
          {},
          reserved ? "Reserved for HAT identification EEPROM"
                   : "Peripheral pin; raw GPIO mapping unavailable"};
}
inline PinInfo Digital(uint32_t n,
                       std::string signal,
                       std::string chip,
                       std::string line,
                       uint32_t offset,
                       std::initializer_list<PinFunction> alternatives,
                       std::optional<PwmPinInfo> pwm = std::nullopt,
                       std::string description = {}) {
  PinInfo pin{n,
              std::move(signal),
              PinKind::kGpio,
              3300,
              {PinFunction::kDigitalInput, PinFunction::kDigitalOutput},
              std::move(chip),
              std::move(line),
              offset,
              std::move(pwm),
              std::move(description),
              "3.3V logic; not 5V tolerant. Configure pinmux externally; load limits unqualified"};
  pin.functions.insert(pin.functions.end(), alternatives.begin(), alternatives.end());
  return pin;
}
}  // namespace robot::board::profiles
