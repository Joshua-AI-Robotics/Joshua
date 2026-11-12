// Orin pin mapping derived from NVIDIA Jetson.GPIO (MIT).
// Copyright (c) 2018-2025 NVIDIA CORPORATION. See THIRD_PARTY_NOTICES.md.
#include "robot/board/pin/pin_resource.h"

#include <set>
#include <utility>

#include "absl/strings/str_cat.h"
#include "utils/status_macros.h"

namespace robot::board {
namespace {

PinProfile Header(std::string id,
                  std::string model,
                  std::string connector,
                  std::vector<std::string> compatible,
                  std::vector<std::string> sources) {
  PinProfile p{std::move(id),
               1,
               std::move(model),
               std::move(connector),
               std::move(compatible),
               {},
               std::move(sources)};
  for (uint32_t n = 1; n <= 40; ++n) {
    PinInfo pin{n, "RESERVED", PinKind::kReserved, 0};
    pin.restrictions = "Peripheral-only or undocumented GPIO mapping; digital access rejected";
    if (n == 1 || n == 17 || n == 2 || n == 4) {
      pin.kind = PinKind::kPower;
      pin.voltage_mv = (n == 1 || n == 17) ? 3300 : 5000;
      pin.signal = pin.voltage_mv == 3300 ? "3V3" : "5V";
      pin.restrictions = "Power supply; never a GPIO";
    }
    if (n == 6 || n == 9 || n == 14 || n == 20 || n == 25 || n == 30 || n == 34 || n == 39) {
      pin.kind = PinKind::kGround;
      pin.signal = "GND";
      pin.restrictions = "Ground; never a GPIO";
    }
    p.pins.push_back(std::move(pin));
  }
  return p;
}

void Gpio(PinProfile& p,
          uint32_t n,
          std::string signal,
          std::string chip,
          std::string line,
          uint32_t offset,
          std::string alternatives) {
  p.pins[n - 1] =
      PinInfo{n,
              std::move(signal),
              PinKind::kGpio,
              3300,
              true,
              true,
              std::move(chip),
              std::move(line),
              offset,
              std::move(alternatives),
              "3.3V logic; not 5V tolerant. Configure pinmux externally; load limits unqualified"};
}

PinProfile Pi5() {
  auto p =
      Header("raspberrypi/pi5",
             "Raspberry Pi 5 Model B, standard 40-pin header",
             "J8",
             {"raspberrypi,5-model-b"},
             {"https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio",
              "https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm64/boot/dts/broadcom/"
              "bcm2712-rpi-5-b.dts",
              "https://github.com/raspberrypi/linux/blob/rpi-6.6.y/drivers/pinctrl/pinctrl-rp1.c"});
  struct Mapping {
    uint32_t pin;
    uint32_t gpio;
    const char* alternatives;
  };
  const Mapping pins[] = {{3, 2, "I2C SDA"},
                          {5, 3, "I2C SCL"},
                          {7, 4, "GPCLK"},
                          {8, 14, "UART TX"},
                          {10, 15, "UART RX"},
                          {11, 17, ""},
                          {12, 18, "PWM / I2S clock"},
                          {13, 27, ""},
                          {15, 22, ""},
                          {16, 23, ""},
                          {18, 24, ""},
                          {19, 10, "SPI MOSI"},
                          {21, 9, "SPI MISO"},
                          {22, 25, ""},
                          {23, 11, "SPI clock"},
                          {24, 8, "SPI CS0"},
                          {26, 7, "SPI CS1"},
                          {29, 5, ""},
                          {31, 6, ""},
                          {32, 12, "PWM"},
                          {33, 13, "PWM"},
                          {35, 19, "PWM / I2S frame"},
                          {36, 16, ""},
                          {37, 26, ""},
                          {38, 20, "I2S input"},
                          {40, 21, "I2S output"}};
  for (const auto& m : pins) {
    const auto name = absl::StrCat("GPIO", m.gpio);
    Gpio(p, m.pin, name, "pinctrl-rp1", name, m.gpio, m.alternatives);
  }
  p.pins[26].signal = "ID_SD";
  p.pins[27].signal = "ID_SC";
  p.pins[26].alternatives = p.pins[27].alternatives = "HAT identification EEPROM I2C; reserved";
  return p;
}

PinProfile OrinNano() {
  auto p = Header("nvidia/orin-nano-p3768",
                  "Jetson Orin Nano P3767 on NVIDIA P3768-0000 carrier",
                  "J12",
                  {"nvidia,p3768-0000+p3767-0003",
                   "nvidia,p3768-0000+p3767-0004",
                   "nvidia,p3768-0000+p3767-0005",
                   "nvidia,p3768-0000+p3767-0003-super",
                   "nvidia,p3768-0000+p3767-0004-super",
                   "nvidia,p3768-0000+p3767-0005-super"},
                  {"https://developer.nvidia.com/embedded/learn/jetson-orin-nano-devkit-user-guide/"
                   "hardware_spec.html",
                   "https://github.com/NVIDIA/jetson-gpio/blob/v2.1.12/lib/python/Jetson/GPIO/"
                   "gpio_pin_data.py"});
  // NVIDIA's Orin NX/Nano BOARD -> CVM / Linux line mapping (Jetson.GPIO).
  struct Mapping {
    uint32_t pin;
    uint32_t offset;
    const char* line;
    const char* signal;
  };
  const Mapping pins[] = {{7, 144, "PAC.06", "GPIO09"},    {11, 112, "PR.04", "UART1_RTS"},
                          {12, 50, "PH.07", "I2S0_SCLK"},  {13, 122, "PY.00", "SPI1_SCK"},
                          {15, 85, "PN.01", "GPIO12"},     {16, 126, "PY.04", "SPI1_CS1"},
                          {18, 125, "PY.03", "SPI1_CS0"},  {19, 135, "PZ.05", "SPI0_MOSI"},
                          {21, 134, "PZ.04", "SPI0_MISO"}, {22, 123, "PY.01", "SPI1_MISO"},
                          {23, 133, "PZ.03", "SPI0_SCK"},  {24, 136, "PZ.06", "SPI0_CS0"},
                          {26, 137, "PZ.07", "SPI0_CS1"},  {29, 105, "PQ.05", "GPIO01"},
                          {31, 106, "PQ.06", "GPIO11"},    {32, 41, "PG.06", "GPIO07"},
                          {33, 43, "PH.00", "GPIO13"},     {35, 53, "PI.02", "I2S0_FS"},
                          {36, 113, "PR.05", "UART1_CTS"}, {37, 124, "PY.02", "SPI1_MOSI"},
                          {38, 52, "PI.01", "I2S0_SDIN"},  {40, 51, "PI.00", "I2S0_SDOUT"}};
  for (const auto& m : pins) Gpio(p, m.pin, m.signal, "tegra234-gpio", m.line, m.offset, m.signal);
  p.pins[14].alternatives = "PWM1";
  p.pins[31].alternatives = "PWM7";
  p.pins[32].alternatives = "PWM5";
  p.pins[2].signal = "I2C_SDA";
  p.pins[4].signal = "I2C_SCL";
  p.pins[7].signal = "UART_TX";
  p.pins[9].signal = "UART_RX";
  p.pins[26].signal = "I2C_SDA";
  p.pins[27].signal = "I2C_SCL";
  return p;
}
}  // namespace

std::string PinResource::identity() const {
  return absl::StrCat(info.chip_label, ":", info.line_name);
}

const std::vector<PinProfile>& PinProfiles() {
  static const std::vector<PinProfile> profiles = {Pi5(), OrinNano()};
  return profiles;
}

absl::StatusOr<const PinProfile*> FindPinProfile(const std::string& id, uint32_t version) {
  for (const auto& p : PinProfiles())
    if (p.id == id && p.version == version) return &p;
  return absl::InvalidArgumentError(
      absl::StrCat("Unknown GPIO profile/version: ", id, "@", version));
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
  if (config.direction() == DIGITAL_OUTPUT && found->digital_output) {
    if (!config.has_initial_value() || !config.has_shutdown_value())
      return absl::InvalidArgumentError(
          "Digital output needs explicit initial_value and shutdown_value");
  } else if (config.direction() == DIGITAL_INPUT && found->digital_input) {
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
