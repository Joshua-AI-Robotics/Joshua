#include "robot/board/pin/peripheral_resource.h"

#include "gtest/gtest.h"
namespace robot::board {
namespace {
Board Config() {
  Board b;
  b.set_name("host");
  b.set_board_type(HOST_GPIO);
  auto* h = b.mutable_host_gpio_config();
  h->set_profile_id("raspberrypi/pi5");
  h->set_profile_version(1);
  return b;
}
BoardPeripheral* Pwm(Board* b, uint32_t physical, const char* name) {
  auto* p = b->add_peripherals();
  p->set_name(name);
  p->set_purpose("test PWM");
  p->mutable_pwm()->mutable_pin()->set_header_pin(physical);
  p->mutable_pwm()->set_frequency_hz(1000);
  p->mutable_pwm()->set_initial_duty_cycle(0);
  return p;
}
TEST(PeripheralResourceTest, RejectsSharedPwmChannelsAndGpioMuxConflicts) {
  auto b = Config();
  Pwm(&b, 8, "one");
  EXPECT_TRUE(ResolveBoardPeripherals(b).ok());
  Pwm(&b, 12, "two");
  EXPECT_FALSE(ResolveBoardPeripherals(b).ok());  // Both PWM0 channel 2.
  b = Config();
  Pwm(&b, 12, "one");
  auto* gpio = b.mutable_host_gpio_config()->add_pins();
  gpio->set_name("gpio");
  gpio->set_purpose("same physical pin");
  gpio->mutable_pin()->set_signal("J8.GPIO18");
  gpio->set_direction(DIGITAL_INPUT);
  EXPECT_FALSE(ResolveBoardPeripherals(b).ok());
}
TEST(PeripheralResourceTest, RejectsWrongRoutesBeforeAcquisition) {
  auto b = Config();
  auto* p = b.add_peripherals();
  p->set_name("spi");
  p->set_purpose("SPI target");
  auto* s = p->mutable_spi();
  s->mutable_clock()->set_header_pin(23);
  s->mutable_mosi()->set_header_pin(19);
  s->mutable_miso()->set_header_pin(21);
  s->mutable_chip_select()->set_header_pin(24);
  s->mutable_settings()->set_device("/dev/spidev0.0");
  s->mutable_settings()->set_controller("spi@50000");
  s->mutable_settings()->set_speed_hz(1000000);
  s->mutable_settings()->set_chip_select_index(0);
  ASSERT_TRUE(ResolveBoardPeripherals(b).ok());
  s->mutable_chip_select()->set_header_pin(11);
  EXPECT_FALSE(ResolveBoardPeripherals(b).ok());
  s->mutable_chip_select()->set_header_pin(24);
  s->mutable_settings()->set_chip_select_index(1);
  EXPECT_FALSE(ResolveBoardPeripherals(b).ok());
}
}  // namespace
}  // namespace robot::board
