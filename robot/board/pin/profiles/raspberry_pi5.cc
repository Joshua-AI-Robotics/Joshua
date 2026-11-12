#include "robot/board/pin/profiles/pin_source_helpers.h"
#include "robot/board/pin/profiles/pin_sources.h"

namespace robot::board::profiles {
namespace {
PinInfo Gpio(uint32_t physical,
             uint32_t gpio,
             std::initializer_list<PinFunction> functions,
             std::optional<uint32_t> pwm_channel = std::nullopt) {
  const auto signal = "GPIO" + std::to_string(gpio);
  std::optional<PwmPinInfo> pwm;
  if (pwm_channel) pwm = PwmPinInfo{"rp1_pwm0", *pwm_channel};
  return Digital(physical, signal, "pinctrl-rp1", signal, gpio, functions, pwm);
}
}  // namespace

PinProfile RaspberryPi5() {
  using F = PinFunction;
  PinProfile profile{
      "raspberrypi/pi5",
      1,
      "Raspberry Pi 5 Model B, standard 40-pin header",
      "J8",
      {"raspberrypi,5-model-b"},
      {
          Power(1, 3300),
          Power(2, 5000),
          Gpio(3, 2, {F::kI2c, F::kSpi, F::kUart}),
          Power(4, 5000),
          Gpio(5, 3, {F::kI2c, F::kSpi, F::kUart}),
          Ground(6),
          Gpio(7, 4, {F::kClock, F::kI2c, F::kSpi, F::kUart}),
          Gpio(8, 14, {F::kPwm, F::kI2c, F::kSpi, F::kUart}, 2),
          Ground(9),
          Gpio(10, 15, {F::kPwm, F::kI2c, F::kSpi, F::kUart}, 3),
          Gpio(11, 17, {F::kSpi, F::kUart}),
          Gpio(12, 18, {F::kPwm, F::kI2s, F::kSpi, F::kClock}, 2),
          Gpio(13, 27, {F::kI2s, F::kSpi}),
          Ground(14),
          Gpio(15, 22, {F::kI2s, F::kI2c}),
          Gpio(16, 23, {F::kI2s, F::kI2c}),
          Power(17, 3300),
          Gpio(18, 24, {F::kI2s, F::kSpi}),
          Gpio(19, 10, {F::kSpi, F::kI2c, F::kUart}),
          Ground(20),
          Gpio(21, 9, {F::kSpi, F::kI2c, F::kUart}),
          Gpio(22, 25, {F::kI2s, F::kSpi}),
          Gpio(23, 11, {F::kSpi, F::kI2c, F::kUart}),
          Gpio(24, 8, {F::kSpi, F::kI2c, F::kUart}),
          Ground(25),
          Gpio(26, 7, {F::kSpi, F::kI2c, F::kUart}),
          Peripheral(27, "ID_SD", F::kI2c, true),
          Peripheral(28, "ID_SC", F::kI2c, true),
          Gpio(29, 5, {F::kClock, F::kI2c, F::kSpi, F::kUart}),
          Ground(30),
          Gpio(31, 6, {F::kClock, F::kI2c, F::kSpi, F::kUart}),
          Gpio(32, 12, {F::kPwm, F::kI2c, F::kSpi, F::kUart}, 0),
          Gpio(33, 13, {F::kPwm, F::kI2c, F::kSpi, F::kUart}, 1),
          Ground(34),
          Gpio(35, 19, {F::kPwm, F::kI2s, F::kSpi}, 3),
          Gpio(36, 16, {F::kSpi, F::kUart}),
          Gpio(37, 26, {F::kI2s, F::kSpi}),
          Gpio(38, 20, {F::kI2s, F::kSpi, F::kClock}),
          Ground(39),
          Gpio(40, 21, {F::kI2s, F::kSpi, F::kClock}),
      },
      {"https://www.raspberrypi.com/documentation/computers/raspberry-pi.html#gpio",
       "https://github.com/raspberrypi/utils/blob/master/pinctrl/gpiochip_rp1.c",
       "https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm64/boot/dts/broadcom/"
       "rp1.dtsi"}};
  using Function = PinFunction;
  profile.peripheral_routes = {
      {Function::kSpi, {23, 19, 21, 24}, 0},
      {Function::kSpi, {23, 19, 21, 26}, 1},
      {Function::kI2c, {3, 5}, std::nullopt},
      {Function::kUart, {8, 10}, std::nullopt},
  };
  return profile;
}
}  // namespace robot::board::profiles
