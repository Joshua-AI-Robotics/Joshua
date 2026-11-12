#include "robot/board/pin/pin_source.h"

#include <algorithm>

#include "absl/strings/str_cat.h"
#include "robot/board/pin/profiles/pin_sources.h"

namespace robot::board {

const char* PinFunctionName(PinFunction function) {
  switch (function) {
    case PinFunction::kDigitalInput:
      return "digital input";
    case PinFunction::kDigitalOutput:
      return "digital output";
    case PinFunction::kPwm:
      return "PWM";
    case PinFunction::kI2c:
      return "I2C";
    case PinFunction::kSpi:
      return "SPI";
    case PinFunction::kUart:
      return "UART";
    case PinFunction::kI2s:
      return "I2S";
    case PinFunction::kClock:
      return "clock";
  }
  return "unknown";
}

bool PinInfo::Supports(PinFunction function) const {
  return std::find(functions.begin(), functions.end(), function) != functions.end();
}

absl::StatusOr<const PinInfo*> PinProfile::FindPin(uint32_t number,
                                                   const std::string& selected) const {
  const PinInfo* found = nullptr;
  for (const auto& pin : pins) {
    const auto& header = pin.connector.empty() ? connector : pin.connector;
    if (pin.header_pin != number || (!selected.empty() && selected != header)) continue;
    if (found)
      return absl::InvalidArgumentError("Physical pin number is ambiguous; select its connector");
    found = &pin;
  }
  if (found) return found;
  return absl::NotFoundError(absl::StrCat(id, " has no physical pin ", selected, ":", number));
}

absl::StatusOr<const PinInfo*> PinProfile::RequirePin(uint32_t number,
                                                      PinFunction function,
                                                      const std::string& connector) const {
  auto pin = FindPin(number, connector);
  if (!pin.ok()) return pin.status();
  if ((*pin)->kind == PinKind::kReserved || !(*pin)->Supports(function))
    return absl::FailedPreconditionError(absl::StrCat(id,
                                                      " pin ",
                                                      connector,
                                                      ":",
                                                      number,
                                                      " is reserved or has no documented ",
                                                      PinFunctionName(function),
                                                      " function"));
  return *pin;
}

const std::vector<PinProfile>& PinProfiles() {
  static const std::vector<PinProfile> catalog = {profiles::RaspberryPi5(),
                                                  profiles::JetsonOrinNanoP3768(),
                                                  profiles::Teensy41(),
                                                  profiles::Esp32DevKitC()};
  return catalog;
}

absl::StatusOr<const PinProfile*> FindPinProfile(const std::string& id, uint32_t version) {
  for (const auto& p : PinProfiles())
    if (p.id == id && p.version == version) return &p;
  return absl::InvalidArgumentError(
      absl::StrCat("Unknown pin source/profile version: ", id, "@", version));
}
}  // namespace robot::board
