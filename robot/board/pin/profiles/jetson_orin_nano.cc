// Orin pin mapping derived from NVIDIA Jetson.GPIO (MIT).
// Copyright (c) 2018-2025 NVIDIA CORPORATION. See THIRD_PARTY_NOTICES.md.
#include "robot/board/pin/profiles/pin_source_helpers.h"
#include "robot/board/pin/profiles/pin_sources.h"

namespace robot::board::profiles {
namespace {
PinInfo Gpio(uint32_t physical,
             std::string signal,
             std::string line,
             uint32_t offset,
             std::initializer_list<PinFunction> functions = {},
             std::optional<PwmPinInfo> pwm = std::nullopt) {
  const auto description = signal;
  return Digital(physical,
                 std::move(signal),
                 "tegra234-gpio",
                 std::move(line),
                 offset,
                 functions,
                 std::move(pwm),
                 description);
}
}  // namespace

PinProfile JetsonOrinNanoP3768() {
  using F = PinFunction;
  return {"nvidia/orin-nano-p3768",
          1,
          "Jetson Orin Nano P3767 on NVIDIA P3768-0000 carrier",
          "J12",
          {"nvidia,p3768-0000+p3767-0003",
           "nvidia,p3768-0000+p3767-0004",
           "nvidia,p3768-0000+p3767-0005",
           "nvidia,p3768-0000+p3767-0003-super",
           "nvidia,p3768-0000+p3767-0004-super",
           "nvidia,p3768-0000+p3767-0005-super"},
          {
              Power(1, 3300),
              Power(2, 5000),
              Peripheral(3, "I2C_SDA", F::kI2c),
              Power(4, 5000),
              Peripheral(5, "I2C_SCL", F::kI2c),
              Ground(6),
              Gpio(7, "GPIO09", "PAC.06", 144),
              Peripheral(8, "UART_TX", F::kUart),
              Ground(9),
              Peripheral(10, "UART_RX", F::kUart),
              Gpio(11, "UART1_RTS", "PR.04", 112, {F::kUart}),
              Gpio(12, "I2S0_SCLK", "PH.07", 50, {F::kI2s}),
              Gpio(13, "SPI1_SCK", "PY.00", 122, {F::kSpi}),
              Ground(14),
              Gpio(15, "GPIO12", "PN.01", 85, {F::kPwm}, PwmPinInfo{"3280000.pwm", 0}),
              Gpio(16, "SPI1_CS1", "PY.04", 126, {F::kSpi}),
              Power(17, 3300),
              Gpio(18, "SPI1_CS0", "PY.03", 125, {F::kSpi}),
              Gpio(19, "SPI0_MOSI", "PZ.05", 135, {F::kSpi}),
              Ground(20),
              Gpio(21, "SPI0_MISO", "PZ.04", 134, {F::kSpi}),
              Gpio(22, "SPI1_MISO", "PY.01", 123, {F::kSpi}),
              Gpio(23, "SPI0_SCK", "PZ.03", 133, {F::kSpi}),
              Gpio(24, "SPI0_CS0", "PZ.06", 136, {F::kSpi}),
              Ground(25),
              Gpio(26, "SPI0_CS1", "PZ.07", 137, {F::kSpi}),
              Peripheral(27, "I2C_SDA", F::kI2c),
              Peripheral(28, "I2C_SCL", F::kI2c),
              Gpio(29, "GPIO01", "PQ.05", 105),
              Ground(30),
              Gpio(31, "GPIO11", "PQ.06", 106),
              Gpio(32, "GPIO07", "PG.06", 41, {F::kPwm}, PwmPinInfo{"32e0000.pwm", 0}),
              Gpio(33, "GPIO13", "PH.00", 43, {F::kPwm}, PwmPinInfo{"32c0000.pwm", 0}),
              Ground(34),
              Gpio(35, "I2S0_FS", "PI.02", 53, {F::kI2s}),
              Gpio(36, "UART1_CTS", "PR.05", 113, {F::kUart}),
              Gpio(37, "SPI1_MOSI", "PY.02", 124, {F::kSpi}),
              Gpio(38, "I2S0_SDIN", "PI.01", 52, {F::kI2s}),
              Ground(39),
              Gpio(40, "I2S0_SDOUT", "PI.00", 51, {F::kI2s}),
          },
          {"https://developer.nvidia.com/embedded/learn/jetson-orin-nano-devkit-user-guide/"
           "hardware_spec.html",
           "https://github.com/NVIDIA/jetson-gpio/blob/v2.1.12/lib/python/Jetson/GPIO/"
           "gpio_pin_data.py"}};
}
}  // namespace robot::board::profiles
