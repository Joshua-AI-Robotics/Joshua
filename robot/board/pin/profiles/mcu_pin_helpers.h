#pragma once

#include <algorithm>
#include <initializer_list>

#include "robot/board/pin/profiles/pin_source_helpers.h"

namespace robot::board::profiles {
inline PinInfo McuPin(std::string header,
                      uint32_t position,
                      uint32_t native,
                      bool output,
                      bool reserved = false,
                      std::initializer_list<PinFunction> extra = {}) {
  PinInfo pin{position,
              "GPIO" + std::to_string(native),
              reserved ? PinKind::kReserved : PinKind::kGpio,
              3300,
              {PinFunction::kDigitalInput}};
  if (output) pin.functions.push_back(PinFunction::kDigitalOutput);
  pin.functions.insert(pin.functions.end(), extra.begin(), extra.end());
  pin.connector = std::move(header);
  pin.firmware_pin = native;
  pin.restrictions = reserved
                         ? "Reserved: boot, flash or JoshuaWire UART resource"
                         : "3.3V logic; not 5V tolerant; PWM/peripheral mux is backend-specific";
  return pin;
}
inline PinInfo HeaderPin(std::string header, PinInfo pin) {
  pin.connector = std::move(header);
  return pin;
}
}  // namespace robot::board::profiles
