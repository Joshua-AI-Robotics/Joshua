#pragma once

#include "robot/board/pin/pin_source.h"

namespace robot::board::profiles {
PinProfile Teensy41();
PinProfile Esp32DevKitC();
PinProfile RaspberryPi5();
PinProfile JetsonOrinNanoP3768();
}  // namespace robot::board::profiles
