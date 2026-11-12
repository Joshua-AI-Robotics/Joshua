#include "robot/board/pin/pin_resource.h"

#include <set>

#include "gtest/gtest.h"

namespace robot::board {
namespace {
DigitalPinConfig Output(uint32_t physical) {
  DigitalPinConfig c;
  c.set_name("ready");
  c.set_purpose("cabinet ready lamp");
  c.mutable_pin()->set_header_pin(physical);
  c.set_direction(DIGITAL_OUTPUT);
  c.set_initial_value(false);
  c.set_shutdown_value(false);
  return c;
}
Board Config() {
  Board b;
  b.set_name("local");
  b.set_board_type(HOST_GPIO);
  auto* c = b.mutable_host_gpio_config();
  c->set_profile_id("raspberrypi/pi5");
  c->set_profile_version(1);
  *c->add_pins() = Output(11);
  return b;
}
TEST(PinResourceTest, CatalogCoversHeadersAndUniqueDigitalIdentities) {
  ASSERT_EQ(PinProfiles().size(), 2);
  for (const auto& profile : PinProfiles()) {
    ASSERT_EQ(profile.pins.size(), 40);
    std::set<uint32_t> positions;
    std::set<std::string> names;
    EXPECT_FALSE(profile.sources.empty());
    EXPECT_FALSE(profile.compatible.empty());
    for (const auto& pin : profile.pins) {
      EXPECT_TRUE(positions.insert(pin.header_pin).second);
      EXPECT_FALSE(pin.restrictions.empty());
      if (pin.kind != PinKind::kGpio) continue;
      EXPECT_TRUE(names.insert(pin.line_name).second);
      EXPECT_EQ(pin.voltage_mv, 3300);
      EXPECT_FALSE(pin.chip_label.empty());
      auto resolved = ResolvePin(profile, Output(pin.header_pin));
      ASSERT_TRUE(resolved.ok()) << resolved.status();
      EXPECT_EQ(resolved->config.purpose(), "cabinet ready lamp");
    }
    EXPECT_EQ(*positions.begin(), 1);
    EXPECT_EQ(*positions.rbegin(), 40);
  }
}
TEST(PinResourceTest, AliasAndPositionResolveToSameClaim) {
  auto c = Config();
  auto first = ResolveHostGpioPins(c);
  ASSERT_TRUE(first.ok());
  auto* pin = c.mutable_host_gpio_config()->mutable_pins(0);
  pin->mutable_pin()->set_signal("J8.GPIO17");
  auto second = ResolveHostGpioPins(c);
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(first->front().identity(), second->front().identity());
  EXPECT_EQ(first->front().info.documented_offset, 17);
  *c.mutable_host_gpio_config()->add_pins() = Output(11);
  c.mutable_host_gpio_config()->mutable_pins(1)->set_name("alias");
  EXPECT_FALSE(ResolveHostGpioPins(c).ok());
}
TEST(PinResourceTest, OrinIsDistinctFromPiAndOriginalNano) {
  auto profile = FindPinProfile("nvidia/orin-nano-p3768", 1);
  ASSERT_TRUE(profile.ok());
  auto pin = ResolvePin(**profile, Output(7));
  ASSERT_TRUE(pin.ok());
  EXPECT_EQ(pin->info.line_name, "PAC.06");
  EXPECT_EQ(pin->info.documented_offset, 144);
  EXPECT_FALSE(FindPinProfile("nvidia/jetson-nano", 1).ok());
  EXPECT_FALSE(FindPinProfile("raspberrypi/pi5", 0).ok());
  EXPECT_FALSE(FindPinProfile("raspberrypi/pi5", 2).ok());
}
TEST(PinResourceTest, RejectsNonGpioUnknownAndUnqualifiedReferences) {
  const auto& profile = PinProfiles().front();
  for (uint32_t n : {0, 1, 2, 6, 27, 28, 41}) EXPECT_FALSE(ResolvePin(profile, Output(n)).ok());
  auto c = Output(11);
  c.mutable_pin()->set_signal("GPIO17");
  EXPECT_FALSE(ResolvePin(profile, c).ok());
  c.mutable_pin()->clear_address();
  EXPECT_FALSE(ResolvePin(profile, c).ok());
}
TEST(PinResourceTest, RejectsMissingPurposeDirectionAndOutputPolicies) {
  const auto& profile = PinProfiles().front();
  auto c = Output(11);
  c.clear_purpose();
  EXPECT_FALSE(ResolvePin(profile, c).ok());
  c = Output(11);
  c.clear_initial_value();
  EXPECT_FALSE(ResolvePin(profile, c).ok());
  c = Output(11);
  c.clear_shutdown_value();
  EXPECT_FALSE(ResolvePin(profile, c).ok());
  c = Output(11);
  c.set_direction(DIGITAL_INPUT);
  EXPECT_FALSE(ResolvePin(profile, c).ok());
  c.clear_initial_value();
  c.clear_shutdown_value();
  EXPECT_TRUE(ResolvePin(profile, c).ok());
  c.set_direction(DIGITAL_DIRECTION_UNSPECIFIED);
  EXPECT_FALSE(ResolvePin(profile, c).ok());
}
TEST(PinResourceTest, RejectsMotionAndFirmwareSettings) {
  auto c = Config();
  EXPECT_TRUE(ResolveHostGpioPins(c).ok());
  c.add_channels()->set_drive(STEP_DIR);
  EXPECT_FALSE(ResolveHostGpioPins(c).ok());
  c = Config();
  c.mutable_comm();
  EXPECT_FALSE(ResolveHostGpioPins(c).ok());
  c = Config();
  c.mutable_firmware();
  EXPECT_FALSE(ResolveHostGpioPins(c).ok());
}
}  // namespace
}  // namespace robot::board
