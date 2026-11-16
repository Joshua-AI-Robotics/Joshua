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

absl::StatusOr<const PinInfo*> PinProfile::FindPin(uint32_t header_pin) const {
  for (const auto& pin : pins)
    if (pin.header_pin == header_pin) return &pin;
  return absl::NotFoundError(absl::StrCat(id, " ", connector, " has no physical pin ", header_pin));
}

absl::StatusOr<const PinInfo*> PinProfile::RequirePin(uint32_t header_pin,
                                                      PinFunction function) const {
  auto pin = FindPin(header_pin);
  if (!pin.ok()) return pin.status();
  if ((*pin)->kind == PinKind::kReserved || !(*pin)->Supports(function))
    return absl::FailedPreconditionError(absl::StrCat(id,
                                                      " ",
                                                      connector,
                                                      " pin ",
                                                      header_pin,
                                                      " is reserved or has no documented ",
                                                      PinFunctionName(function),
                                                      " function"));
  return *pin;
}

const std::vector<PinProfile>& PinProfiles() {
  static const std::vector<PinProfile> catalog = {profiles::RaspberryPi5(),
                                                  profiles::JetsonOrinNanoP3768()};
  return catalog;
}

absl::StatusOr<const PinProfile*> FindPinProfile(const std::string& id, uint32_t version) {
  for (const auto& p : PinProfiles())
    if (p.id == id && p.version == version) return &p;
  return absl::InvalidArgumentError(
      absl::StrCat("Unknown pin source/profile version: ", id, "@", version));
}
}  // namespace robot::board
