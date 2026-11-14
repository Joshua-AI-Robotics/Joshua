#include <algorithm>

#include "robot/board/pin/profiles/mcu_pin_helpers.h"
#include "robot/board/pin/profiles/pin_sources.h"

namespace robot::board::profiles {
namespace {
PinInfo Gpio(const char* header, uint32_t position, uint32_t native) {
  auto pin = McuPin(header, position, native, true);
  const uint32_t pwm[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                          14, 15, 18, 19, 22, 23, 24, 25, 28, 29, 33, 36, 37};
  if (std::find(std::begin(pwm), std::end(pwm), native) != std::end(pwm))
    pin.functions.push_back(PinFunction::kPwm);
  const uint32_t uart[] = {0, 1, 7, 8, 14, 15, 16, 17, 20, 21, 24, 25, 28, 29, 34, 35};
  if (std::find(std::begin(uart), std::end(uart), native) != std::end(uart))
    pin.functions.push_back(PinFunction::kUart);
  if (native == 16 || native == 17 || native == 18 || native == 19 || native == 24 || native == 25)
    pin.functions.push_back(PinFunction::kI2c);
  if (native == 0 || native == 1 || (native >= 10 && native <= 13) ||
      (native >= 26 && native <= 27) || native == 38 || native == 39)
    pin.functions.push_back(PinFunction::kSpi);
  return pin;
}
}  // namespace
PinProfile Teensy41() {
  auto vin = HeaderPin("RIGHT", Power(1, 5000));
  vin.signal = "VIN";
  vin.restrictions = "VIN supply input, documented range 3.6..5.5V";
  // Component-side view, USB connector at the top. Each 24-pin side header is
  // numbered from the USB end. Underside pads/USB-host/Ethernet are not claimed.
  return {"pjrc/teensy41",
          1,
          "PJRC Teensy 4.1, two outer 24-pin headers (USB at top)",
          "",
          {},
          {HeaderPin("LEFT", Ground(1)),
           Gpio("LEFT", 2, 0),
           Gpio("LEFT", 3, 1),
           Gpio("LEFT", 4, 2),
           Gpio("LEFT", 5, 3),
           Gpio("LEFT", 6, 4),
           Gpio("LEFT", 7, 5),
           Gpio("LEFT", 8, 6),
           Gpio("LEFT", 9, 7),
           Gpio("LEFT", 10, 8),
           Gpio("LEFT", 11, 9),
           Gpio("LEFT", 12, 10),
           Gpio("LEFT", 13, 11),
           Gpio("LEFT", 14, 12),
           HeaderPin("LEFT", Power(15, 3300)),
           Gpio("LEFT", 16, 24),
           Gpio("LEFT", 17, 25),
           Gpio("LEFT", 18, 26),
           Gpio("LEFT", 19, 27),
           Gpio("LEFT", 20, 28),
           Gpio("LEFT", 21, 29),
           Gpio("LEFT", 22, 30),
           Gpio("LEFT", 23, 31),
           Gpio("LEFT", 24, 32),
           vin,
           HeaderPin("RIGHT", Ground(2)),
           HeaderPin("RIGHT", Power(3, 3300)),
           Gpio("RIGHT", 4, 23),
           Gpio("RIGHT", 5, 22),
           Gpio("RIGHT", 6, 21),
           Gpio("RIGHT", 7, 20),
           Gpio("RIGHT", 8, 19),
           Gpio("RIGHT", 9, 18),
           Gpio("RIGHT", 10, 17),
           Gpio("RIGHT", 11, 16),
           Gpio("RIGHT", 12, 15),
           Gpio("RIGHT", 13, 14),
           Gpio("RIGHT", 14, 13),
           HeaderPin("RIGHT", Ground(15)),
           Gpio("RIGHT", 16, 41),
           Gpio("RIGHT", 17, 40),
           Gpio("RIGHT", 18, 39),
           Gpio("RIGHT", 19, 38),
           Gpio("RIGHT", 20, 37),
           Gpio("RIGHT", 21, 36),
           Gpio("RIGHT", 22, 35),
           Gpio("RIGHT", 23, 34),
           Gpio("RIGHT", 24, 33)},
          {"https://www.pjrc.com/teensy/card11a_rev4_web.pdf",
           "https://www.pjrc.com/teensy/td_pulse.html"},
          PinBackend::kMcu};
}
}  // namespace robot::board::profiles
