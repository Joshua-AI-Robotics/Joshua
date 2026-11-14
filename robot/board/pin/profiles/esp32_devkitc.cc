#include "robot/board/pin/profiles/mcu_pin_helpers.h"
#include "robot/board/pin/profiles/pin_sources.h"

namespace robot::board::profiles {
namespace {
PinInfo Gpio(const char* header, uint32_t position, uint32_t native) {
  const bool output = native < 34;
  const bool reserved = native == 0 || native == 1 || native == 2 || native == 3 || native == 5 ||
                        native == 12 || native == 15 || (native >= 6 && native <= 11);
  auto pin = McuPin(header, position, native, output, reserved);
  if (output && !reserved) {
    pin.functions.insert(
        pin.functions.end(),
        {PinFunction::kPwm, PinFunction::kI2c, PinFunction::kSpi, PinFunction::kUart});
    pin.alternatives = "ESP32 GPIO matrix; peripheral backend validates routed signals";
  }
  return pin;
}
}  // namespace
PinProfile Esp32DevKitC() {
  auto enable = HeaderPin("J2", PinInfo{2, "EN", PinKind::kReserved, 3300});
  enable.restrictions = "Chip enable/reset; never a general GPIO";
  return {"espressif/esp32-devkitc-v4-wroom32",
          1,
          "Espressif ESP32-DevKitC V4 with ESP32-WROOM-32 module",
          "",
          {},
          {HeaderPin("J2", Power(1, 3300)),
           enable,
           Gpio("J2", 3, 36),
           Gpio("J2", 4, 39),
           Gpio("J2", 5, 34),
           Gpio("J2", 6, 35),
           Gpio("J2", 7, 32),
           Gpio("J2", 8, 33),
           Gpio("J2", 9, 25),
           Gpio("J2", 10, 26),
           Gpio("J2", 11, 27),
           Gpio("J2", 12, 14),
           Gpio("J2", 13, 12),
           HeaderPin("J2", Ground(14)),
           Gpio("J2", 15, 13),
           Gpio("J2", 16, 9),
           Gpio("J2", 17, 10),
           Gpio("J2", 18, 11),
           HeaderPin("J2", Power(19, 5000)),
           HeaderPin("J3", Ground(1)),
           Gpio("J3", 2, 23),
           Gpio("J3", 3, 22),
           Gpio("J3", 4, 1),
           Gpio("J3", 5, 3),
           Gpio("J3", 6, 21),
           HeaderPin("J3", Ground(7)),
           Gpio("J3", 8, 19),
           Gpio("J3", 9, 18),
           Gpio("J3", 10, 5),
           Gpio("J3", 11, 17),
           Gpio("J3", 12, 16),
           Gpio("J3", 13, 4),
           Gpio("J3", 14, 0),
           Gpio("J3", 15, 2),
           Gpio("J3", 16, 15),
           Gpio("J3", 17, 8),
           Gpio("J3", 18, 7),
           Gpio("J3", 19, 6)},
          {"https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32/esp32-devkitc/"
           "user_guide.html"},
          PinBackend::kMcu};
}
}  // namespace robot::board::profiles
