#include "robot/board/pin/pin_source.h"

#include <utility>

#include "gtest/gtest.h"

namespace robot::board {
namespace {
TEST(PinSourceTest, PhysicalPinLookupIncludesPowerGroundAndPeripheralPins) {
  for (const auto& profile : PinProfiles()) {
    if (profile.backend != PinBackend::kLinux) continue;
    ASSERT_EQ(profile.pins.size(), 40);
    for (uint32_t n = 1; n <= 40; ++n) {
      auto pin = profile.FindPin(n);
      ASSERT_TRUE(pin.ok()) << pin.status();
      EXPECT_EQ((*pin)->header_pin, n);
    }
    EXPECT_EQ((*profile.FindPin(1))->kind, PinKind::kPower);
    EXPECT_EQ((*profile.FindPin(1))->voltage_mv, 3300);
    EXPECT_EQ((*profile.FindPin(2))->kind, PinKind::kPower);
    EXPECT_EQ((*profile.FindPin(2))->voltage_mv, 5000);
    EXPECT_EQ((*profile.FindPin(6))->kind, PinKind::kGround);
    EXPECT_FALSE(profile.FindPin(0).ok());
    EXPECT_FALSE(profile.FindPin(41).ok());
    EXPECT_FALSE(profile.RequirePin(2, PinFunction::kPwm).ok());
    EXPECT_FALSE(profile.RequirePin(6, PinFunction::kDigitalOutput).ok());
    for (const auto& pin : profile.pins) {
      EXPECT_EQ(pin.Supports(PinFunction::kPwm), pin.pwm.has_value());
      if (!pin.pwm) continue;
      EXPECT_FALSE(pin.pwm->controller.empty());
      EXPECT_TRUE(pin.Supports(PinFunction::kDigitalOutput));
    }
  }
}
TEST(PinSourceTest, Pi5PwmUsesRp1ChannelsAndPinsMayShareOnePwmResource) {
  auto source = FindPinProfile("raspberrypi/pi5", 1);
  ASSERT_TRUE(source.ok());
  for (auto [physical, channel] :
       {std::pair{8u, 2u}, {10u, 3u}, {12u, 2u}, {32u, 0u}, {33u, 1u}, {35u, 3u}}) {
    auto pin = (*source)->RequirePin(physical, PinFunction::kPwm);
    ASSERT_TRUE(pin.ok()) << pin.status();
    ASSERT_TRUE((*pin)->pwm.has_value());
    EXPECT_EQ((*pin)->pwm->controller, "rp1_pwm0");
    EXPECT_EQ((*pin)->pwm->channel, channel);
  }
  // Physical 8 can be UART, digital GPIO or PWM, depending on pinmux.
  auto uart = (*source)->RequirePin(8, PinFunction::kUart);
  ASSERT_TRUE(uart.ok());
  EXPECT_TRUE((*uart)->Supports(PinFunction::kDigitalOutput));
  EXPECT_FALSE((*source)->RequirePin(11, PinFunction::kPwm).ok());
  EXPECT_FALSE((*source)->RequirePin(27, PinFunction::kI2c).ok());  // HAT reserved.
}
TEST(PinSourceTest, OrinPwmAndPeripheralMappingsAreBoardSpecific) {
  auto source = FindPinProfile("nvidia/orin-nano-p3768", 1);
  ASSERT_TRUE(source.ok());
  for (const auto& [physical, controller] :
       {std::pair{15u, "3280000.pwm"}, {32u, "32e0000.pwm"}, {33u, "32c0000.pwm"}}) {
    auto pin = (*source)->RequirePin(physical, PinFunction::kPwm);
    ASSERT_TRUE(pin.ok()) << pin.status();
    ASSERT_TRUE((*pin)->pwm.has_value());
    EXPECT_EQ((*pin)->pwm->controller, controller);
    EXPECT_EQ((*pin)->pwm->channel, 0);
  }
  auto i2c = (*source)->RequirePin(3, PinFunction::kI2c);
  ASSERT_TRUE(i2c.ok());
  EXPECT_EQ((*i2c)->kind, PinKind::kPeripheral);
  EXPECT_FALSE((*i2c)->Supports(PinFunction::kDigitalInput));
  EXPECT_FALSE((*source)->RequirePin(12, PinFunction::kPwm).ok());
  EXPECT_EQ((*(*source)->RequirePin(7, PinFunction::kDigitalOutput))->line_name, "PAC.06");
}
}  // namespace
}  // namespace robot::board
