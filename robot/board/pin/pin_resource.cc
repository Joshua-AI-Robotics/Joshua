#include "robot/board/pin/pin_resource.h"

#include <set>
#include <utility>

#include "absl/strings/str_cat.h"
#include "utils/status_macros.h"

namespace robot::board {
std::string PinResource::identity() const {
  if (info.firmware_pin) return absl::StrCat(profile_id, ":mcu:", *info.firmware_pin);
  return absl::StrCat(info.chip_label, ":", info.line_name);
}

absl::StatusOr<const PinInfo*> ResolvePinReference(const PinProfile& profile, const PinRef& ref) {
  if (ref.address_case() == PinRef::kHeaderPin)
    return profile.FindPin(ref.header_pin(), ref.connector());
  const PinInfo* found = nullptr;
  for (const auto& pin : profile.pins) {
    const auto& header = pin.connector.empty() ? profile.connector : pin.connector;
    if ((ref.address_case() == PinRef::kSignal && ref.signal() == header + "." + pin.signal) ||
        (ref.address_case() == PinRef::kFirmwarePin && pin.firmware_pin &&
         *pin.firmware_pin == ref.firmware_pin())) {
      if (found) return absl::InvalidArgumentError("Ambiguous pin reference");
      found = &pin;
    }
  }
  if (!found) return absl::InvalidArgumentError("Unknown pin reference");
  if (!ref.connector().empty() &&
      ref.connector() != (found->connector.empty() ? profile.connector : found->connector))
    return absl::InvalidArgumentError("Pin reference connector mismatch");
  return found;
}

absl::StatusOr<Board> ResolveMcuBoardPins(const Board& input) {
  Board config = input;
  if (!config.has_pin_profile()) {
    for (const auto& channel : config.channels())
      if (channel.has_step_dir_wiring())
        return absl::InvalidArgumentError("Named MCU wiring requires pin_profile");
    return config;
  }
  ABSL_ASSIGN_OR_RETURN(const auto* profile,
                        FindPinProfile(config.pin_profile().id(), config.pin_profile().version()));
  const bool compatible =
      (config.board_type() == TEENSY41 && profile->id == "pjrc/teensy41") ||
      (config.board_type() == ESP32 && profile->id == "espressif/esp32-devkitc-v4-wroom32");
  if (!compatible || profile->backend != PinBackend::kMcu)
    return absl::InvalidArgumentError("MCU pin profile does not match board type");
  std::set<uint32_t> claims;
  for (auto& channel : *config.mutable_channels()) {
    if (channel.drive() != STEP_DIR || !channel.has_step_dir())
      return absl::InvalidArgumentError("MCU profile wiring currently supports STEP_DIR");
    auto* setup = channel.mutable_step_dir();
    uint32_t ids[3];
    if (channel.has_step_dir_wiring()) {
      if (setup->has_step_pin() || setup->has_dir_pin() || setup->has_enable_pin())
        return absl::InvalidArgumentError("Do not mix named wiring and legacy MCU numeric pins");
      const auto& wiring = channel.step_dir_wiring();
      const PinRef* refs[] = {&wiring.step(), &wiring.direction(), &wiring.enable()};
      for (int i = 0; i < 3; ++i) {
        ABSL_ASSIGN_OR_RETURN(const auto* pin, ResolvePinReference(*profile, *refs[i]));
        if (pin->kind != PinKind::kGpio || !pin->Supports(PinFunction::kDigitalOutput) ||
            !pin->firmware_pin)
          return absl::InvalidArgumentError("STEP_DIR requires an available MCU output pin");
        ids[i] = *pin->firmware_pin;
      }
      channel.clear_step_dir_wiring();
    } else {
      ids[0] = setup->step_pin();
      ids[1] = setup->dir_pin();
      ids[2] = setup->enable_pin();
      for (const auto id : ids) {
        PinRef ref;
        ref.set_firmware_pin(id);
        ABSL_ASSIGN_OR_RETURN(const auto* pin, ResolvePinReference(*profile, ref));
        if (pin->kind != PinKind::kGpio || !pin->Supports(PinFunction::kDigitalOutput))
          return absl::InvalidArgumentError("Configured MCU pin is unavailable for output");
      }
    }
    for (const auto id : ids)
      if (!claims.insert(id).second)
        return absl::InvalidArgumentError("Duplicate MCU pin claim across channels/signals");
    setup->set_step_pin(ids[0]);
    setup->set_dir_pin(ids[1]);
    setup->set_enable_pin(ids[2]);
  }
  return config;
}

absl::StatusOr<PinResource> ResolvePin(const PinProfile& profile, const DigitalPinConfig& config) {
  ABSL_ASSIGN_OR_RETURN(const auto* found, ResolvePinReference(profile, config.pin()));
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
  return PinResource{profile.id,
                     profile.version,
                     found->connector.empty() ? profile.connector : found->connector,
                     *found,
                     config};
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
  if (profile->backend != PinBackend::kLinux)
    return absl::InvalidArgumentError("HOST_GPIO requires a Linux pin source");
  if (c.pins().empty() && config.peripherals().empty())
    return absl::InvalidArgumentError("HOST_GPIO has no configured pins/peripherals");
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
