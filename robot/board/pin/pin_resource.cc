#include "robot/board/pin/pin_resource.h"

#include <set>
#include <utility>

#include "absl/strings/str_cat.h"
#include "utils/status_macros.h"

namespace robot::board {
std::string PinResource::identity() const {
  return absl::StrCat(info.chip_label, ":", info.line_name);
}

absl::StatusOr<PinResource> ResolvePin(const PinProfile& profile, const DigitalPinConfig& config) {
  const PinInfo* found = nullptr;
  for (const auto& pin : profile.pins) {
    const auto& ref = config.pin();
    if ((ref.address_case() == PinRef::kHeaderPin && ref.header_pin() == pin.header_pin) ||
        (ref.address_case() == PinRef::kSignal &&
         ref.signal() == profile.connector + "." + pin.signal)) {
      if (found) return absl::InvalidArgumentError("Ambiguous GPIO signal alias");
      found = &pin;
    }
  }
  if (!found) return absl::InvalidArgumentError("Unknown GPIO pin reference");
  if (found->kind != PinKind::kGpio)
    return absl::InvalidArgumentError("Power, ground, reserved or peripheral-only pin is not GPIO");
  if (config.name().empty() || config.purpose().empty())
    return absl::InvalidArgumentError("Digital pin needs name and purpose");
  if (!DigitalBias_IsValid(config.bias()))
    return absl::InvalidArgumentError("Unknown digital bias");
  if (config.direction() == DIGITAL_OUTPUT && found->Supports(PinFunction::kDigitalOutput)) {
    if (!config.has_initial_value() || !config.has_shutdown_value())
      return absl::InvalidArgumentError(
          "Digital output needs explicit initial_value and shutdown_value");
  } else if (config.direction() == DIGITAL_INPUT && found->Supports(PinFunction::kDigitalInput)) {
    if (config.has_initial_value() || config.has_shutdown_value())
      return absl::InvalidArgumentError("Digital input cannot declare output values");
  } else {
    return absl::InvalidArgumentError("Unsupported digital direction");
  }
  return PinResource{profile.id, profile.version, profile.connector, *found, config};
}

absl::StatusOr<std::vector<PinResource>> ResolveHostGpioPins(const Board& config) {
  if (config.board_type() != HOST_GPIO || !config.has_host_gpio_config() || config.name().empty())
    return absl::InvalidArgumentError("HOST_GPIO requires a board name and host_gpio_config");
  if (config.has_comm() || config.has_firmware() || config.has_joshua_wire_host_policy() ||
      config.protocol() != BOARD_PROTOCOL_UNSPECIFIED || !config.channels().empty())
    return absl::InvalidArgumentError(
        "HOST_GPIO digital pins do not accept comm, firmware, protocol or motor channels");
  const auto& c = config.host_gpio_config();
  ABSL_ASSIGN_OR_RETURN(const auto* profile, FindPinProfile(c.profile_id(), c.profile_version()));
  if (c.pins().empty()) return absl::InvalidArgumentError("HOST_GPIO has no pins");
  std::set<std::string> names, claims;
  std::vector<PinResource> pins;
  for (const auto& pin : c.pins()) {
    auto resolved = ResolvePin(*profile, pin);
    if (!resolved.ok())
      return absl::InvalidArgumentError(absl::StrCat(
          "Board '", config.name(), "' pin '", pin.name(), "': ", resolved.status().message()));
    if (!names.insert(pin.name()).second || !claims.insert(resolved->identity()).second)
      return absl::InvalidArgumentError("Duplicate digital pin name or physical GPIO claim");
    pins.push_back(std::move(*resolved));
  }
  return pins;
}
}  // namespace robot::board
