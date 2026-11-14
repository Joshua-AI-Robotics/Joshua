#include "robot/board/pin/peripheral_resource.h"

#include <cmath>
#include <map>
#include <set>

#include "robot/comm/linux_bus/bus_validation.h"
#include "robot/comm/serial/serial_config_validation.h"
#include "utils/status_macros.h"
namespace robot::board {
std::string PinIdentity(const PinProfile& profile, const PinInfo& pin) {
  if (pin.firmware_pin) return "native:" + std::to_string(*pin.firmware_pin);
  if (!pin.chip_label.empty()) return pin.chip_label + ":" + pin.line_name;
  return profile.id + ":" + (pin.connector.empty() ? profile.connector : pin.connector) + ":" +
         std::to_string(pin.header_pin);
}
absl::StatusOr<std::vector<PeripheralResource>> ResolveBoardPeripherals(const Board& input) {
  std::vector<PeripheralResource> result;
  if (input.peripherals().empty()) return result;
  ABSL_ASSIGN_OR_RETURN(auto board, ResolveMcuBoardPins(input));
  const bool host = board.board_type() == HOST_GPIO;
  const auto id = host ? board.host_gpio_config().profile_id() : board.pin_profile().id();
  const auto version =
      host ? board.host_gpio_config().profile_version() : board.pin_profile().version();
  ABSL_ASSIGN_OR_RETURN(const auto* profile, FindPinProfile(id, version));
  if ((host && profile->backend != PinBackend::kLinux) ||
      (!host && (profile->backend != PinBackend::kMcu ||
                 (board.board_type() != TEENSY41 && board.board_type() != ESP32))))
    return absl::InvalidArgumentError("Peripheral board/profile mismatch");
  std::set<std::string> names;
  std::set<uint32_t> functions;
  std::map<std::string, std::string> claims;
  auto claim = [&](const PeripheralClaim& c) -> absl::Status {
    const auto [it, inserted] = claims.emplace(c.identity, c.sharing);
    if (!inserted && (c.sharing.empty() || it->second != c.sharing))
      return absl::InvalidArgumentError("Conflicting board resource: " + c.identity);
    return absl::OkStatus();
  };
  if (host) {
    ABSL_ASSIGN_OR_RETURN(auto gpio, ResolveHostGpioPins(board));
    for (const auto& p : gpio) {
      names.insert(p.config.name());
      ABSL_RETURN_IF_ERROR(claim({PinIdentity(*profile, p.info), {}}));
    }
  } else {
    for (const auto& c : board.channels())
      for (auto id : {c.step_dir().step_pin(), c.step_dir().dir_pin(), c.step_dir().enable_pin()})
        ABSL_RETURN_IF_ERROR(claim({"native:" + std::to_string(id), {}}));
  }
  for (const auto& c : board.peripherals()) {
    if (c.name().empty() || c.purpose().empty() || !names.insert(c.name()).second)
      return absl::InvalidArgumentError("Peripheral needs a unique name and purpose");
    if (host ? c.has_function_id()
             : (!c.has_function_id() || c.function_id() < 192 || c.function_id() >= 200 ||
                !functions.insert(c.function_id()).second))
      return absl::InvalidArgumentError(
          "MCU peripheral requires unique firmware slot 192..199; Linux omits function_id");
    PeripheralResource resource;
    resource.config = c;
    std::set<std::string> local;
    auto pin =
        [&](const PinRef& ref, PinFunction function, const std::string& sharing) -> absl::Status {
      ABSL_ASSIGN_OR_RETURN(const auto* p, ResolvePinReference(*profile, ref));
      if (p->kind == PinKind::kReserved || !p->Supports(function) || (!host && !p->firmware_pin))
        return absl::InvalidArgumentError("Pin does not support required peripheral function");
      const auto identity = PinIdentity(*profile, *p);
      if (!local.insert(identity).second)
        return absl::InvalidArgumentError(
            "A peripheral cannot reuse one pin for different signals");
      PeripheralClaim q{identity, sharing};
      ABSL_RETURN_IF_ERROR(claim(q));
      resource.pins.push_back(*p);
      resource.claims.push_back(std::move(q));
      return absl::OkStatus();
    };
    switch (c.operation_case()) {
      case BoardPeripheral::kPwm: {
        const auto& p = c.pwm();
        if (!p.frequency_hz() || p.frequency_hz() > 1000000 || !p.has_initial_duty_cycle() ||
            !std::isfinite(p.initial_duty_cycle()) || p.initial_duty_cycle() < 0 ||
            p.initial_duty_cycle() > 1)
          return absl::InvalidArgumentError(
              "PWM requires frequency 1..1000000 and explicit normalized initial duty");
        ABSL_RETURN_IF_ERROR(pin(p.pin(), PinFunction::kPwm, {}));
        if (host) {
          if (!resource.pins[0].pwm)
            return absl::InvalidArgumentError("No documented PWM controller/channel mapping");
          const auto& pwm = *resource.pins[0].pwm;
          PeripheralClaim q{"pwm:" + pwm.controller + ":" + std::to_string(pwm.channel), {}};
          ABSL_RETURN_IF_ERROR(claim(q));
          resource.claims.push_back(std::move(q));
        }
        break;
      }
      case BoardPeripheral::kSpi: {
        const auto& p = c.spi();
        ABSL_RETURN_IF_ERROR(robot::comm::ValidateSpiSettings(p.settings(), !host));
        if ((host && p.bus_index()) || (!host && p.bus_index() > 1))
          return absl::InvalidArgumentError("Unsupported SPI bus index");
        const auto group =
            "spi:" + (host ? p.settings().controller() : std::to_string(p.bus_index()));
        ABSL_RETURN_IF_ERROR(pin(p.clock(), PinFunction::kSpi, group + ":clock"));
        ABSL_RETURN_IF_ERROR(pin(p.mosi(), PinFunction::kSpi, group + ":mosi"));
        ABSL_RETURN_IF_ERROR(pin(p.miso(), PinFunction::kSpi, group + ":miso"));
        ABSL_RETURN_IF_ERROR(pin(p.chip_select(), PinFunction::kDigitalOutput, {}));
        break;
      }
      case BoardPeripheral::kI2C: {
        const auto& p = c.i2c();
        ABSL_RETURN_IF_ERROR(robot::comm::ValidateI2cSettings(p.settings(), !host));
        if ((host && p.bus_index()) || (!host && p.bus_index() > 1))
          return absl::InvalidArgumentError("Unsupported I2C bus index");
        const auto group =
            "i2c:" + (host ? p.settings().controller() : std::to_string(p.bus_index())) + ":" +
            std::to_string(p.settings().timeout_ms()) + ":" +
            std::to_string(p.settings().frequency_hz());
        ABSL_RETURN_IF_ERROR(pin(p.sda(), PinFunction::kI2c, group + ":sda"));
        ABSL_RETURN_IF_ERROR(pin(p.scl(), PinFunction::kI2c, group + ":scl"));
        ABSL_RETURN_IF_ERROR(
            claim({group + ":address:" + std::to_string(p.settings().address()), {}}));
        break;
      }
      case BoardPeripheral::kUart: {
        const auto& p = c.uart();
        const auto& settings = p.settings();
        if ((host && (p.bus_index() || p.controller().empty())) ||
            (!host &&
             (p.bus_index() < 1 || p.bus_index() > (board.board_type() == TEENSY41 ? 8 : 2) ||
              !settings.port().empty() || !p.controller().empty())))
          return absl::InvalidArgumentError(
              "UART requires Linux controller selector or supported MCU bus 1..8/2");
        if (!settings.baudrate() || settings.baudrate() > 4000000 ||
            settings.message_adapter() != robot::comm::SERIAL_MESSAGE_ADAPTER_UNSPECIFIED ||
            settings.has_exchange_timeout_ms() || settings.has_frame_policy() ||
            settings.post_open_settle_ms())
          return absl::InvalidArgumentError(
              "Peripheral UART requires raw 8N1 settings without framing/reset settling");
        if (host) {
          robot::comm::Comm serial;
          serial.set_comm_type(robot::comm::SERIAL);
          serial.set_transport_type(robot::comm::BYTE_STREAM);
          *serial.mutable_serial_config() = settings;
          ABSL_RETURN_IF_ERROR(robot::comm::ValidateSerialCommConfig(serial));
        }
        ABSL_RETURN_IF_ERROR(pin(p.tx(), PinFunction::kUart, {}));
        ABSL_RETURN_IF_ERROR(pin(p.rx(), PinFunction::kUart, {}));
        break;
      }
      default:
        return absl::InvalidArgumentError("Peripheral operation is absent");
    }
    if (host && !c.has_pwm()) {
      const auto function = c.has_spi()   ? PinFunction::kSpi
                            : c.has_i2c() ? PinFunction::kI2c
                                          : PinFunction::kUart;
      std::vector<uint32_t> physical;
      for (const auto& p : resource.pins) physical.push_back(p.header_pin);
      bool found = false;
      for (const auto& route : profile->peripheral_routes) {
        if (route.function != function || route.pins != physical) continue;
        if (c.has_spi() &&
            (!route.chip_select || *route.chip_select != c.spi().settings().chip_select_index()))
          continue;
        found = true;
      }
      if (!found)
        return absl::InvalidArgumentError(
            "Peripheral signals are not a documented executable header route");
    }
    result.push_back(std::move(resource));
  }
  return result;
}
}  // namespace robot::board
