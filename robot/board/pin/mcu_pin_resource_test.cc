#include "gtest/gtest.h"
#include "robot/board/pin/pin_resource.h"

namespace robot::board {
namespace {
Board Config(BoardType type) {
  Board b;
  b.set_name("mcu");
  b.set_board_type(type);
  auto* p = b.mutable_pin_profile();
  p->set_id(type == TEENSY41 ? "pjrc/teensy41" : "espressif/esp32-devkitc-v4-wroom32");
  p->set_version(1);
  auto* c = b.add_channels();
  c->set_drive(STEP_DIR);
  c->mutable_step_dir()->set_max_pulse_rate_hz(1000);
  auto* w = c->mutable_step_dir_wiring();
  if (type == TEENSY41) {
    w->mutable_step()->set_signal("LEFT.GPIO0");
    w->mutable_direction()->set_signal("LEFT.GPIO2");
    w->mutable_enable()->set_signal("LEFT.GPIO3");
  } else {
    w->mutable_step()->set_connector("J2");
    w->mutable_step()->set_header_pin(9);
    w->mutable_direction()->set_firmware_pin(26);
    w->mutable_enable()->set_signal("J2.GPIO27");
  }
  return b;
}
TEST(McuPinResourceTest, ResolvesPhysicalAliasesAndNativeZeroWithoutChangingInput) {
  for (auto type : {TEENSY41, ESP32}) {
    const auto original = Config(type);
    auto resolved = ResolveMcuBoardPins(original);
    ASSERT_TRUE(resolved.ok()) << resolved.status();
    EXPECT_TRUE(original.channels(0).has_step_dir_wiring());
    EXPECT_FALSE(resolved->channels(0).has_step_dir_wiring());
    EXPECT_EQ(resolved->channels(0).step_dir().step_pin(), type == TEENSY41 ? 0 : 25);
    EXPECT_EQ(resolved->channels(0).step_dir().dir_pin(), type == TEENSY41 ? 2 : 26);
    EXPECT_EQ(resolved->channels(0).step_dir().enable_pin(), type == TEENSY41 ? 3 : 27);
    auto again = ResolveMcuBoardPins(*resolved);
    ASSERT_TRUE(again.ok()) << again.status();
    EXPECT_EQ(resolved->SerializeAsString(), again->SerializeAsString());
  }
}
TEST(McuPinResourceTest, LegacyPinsRemainCompatibleWithoutProfiles) {
  auto b = Config(TEENSY41);
  b.clear_pin_profile();
  b.mutable_channels(0)->clear_step_dir_wiring();
  b.mutable_channels(0)->mutable_step_dir()->set_step_pin(0);
  EXPECT_EQ(ResolveMcuBoardPins(b)->SerializeAsString(), b.SerializeAsString());
}
TEST(McuPinResourceTest, RejectsMixedExplicitZeroAndCrossChannelAliasClaims) {
  auto b = Config(TEENSY41);
  b.mutable_channels(0)->mutable_step_dir()->set_step_pin(0);
  EXPECT_FALSE(ResolveMcuBoardPins(b).ok());
  b = Config(TEENSY41);
  auto copy = b.channels(0);
  copy.set_index(1);
  *b.add_channels() = copy;
  EXPECT_FALSE(ResolveMcuBoardPins(b).ok());
  b = Config(TEENSY41);
  b.clear_pin_profile();
  EXPECT_FALSE(ResolveMcuBoardPins(b).ok());
}
TEST(McuPinResourceTest, RejectsWrongBoardPowerReservedAndInputOnlyPins) {
  auto b = Config(ESP32);
  b.set_board_type(TEENSY41);
  EXPECT_FALSE(ResolveMcuBoardPins(b).ok());
  for (auto id : {0u, 1u, 3u, 6u, 12u, 34u, 36u}) {
    b = Config(ESP32);
    b.mutable_channels(0)->mutable_step_dir_wiring()->mutable_step()->set_firmware_pin(id);
    EXPECT_FALSE(ResolveMcuBoardPins(b).ok()) << id;
  }
  b = Config(ESP32);
  b.mutable_channels(0)->mutable_step_dir_wiring()->mutable_step()->set_header_pin(1);
  EXPECT_FALSE(ResolveMcuBoardPins(b).ok());  // Ambiguous header.
}
TEST(McuPinResourceTest, CatalogIncludesCompleteOuterConnectorsAndInputOnlyFacts) {
  auto teensy = FindPinProfile("pjrc/teensy41", 1);
  auto esp = FindPinProfile("espressif/esp32-devkitc-v4-wroom32", 1);
  ASSERT_TRUE(teensy.ok());
  ASSERT_TRUE(esp.ok());
  EXPECT_EQ((*teensy)->pins.size(), 48);
  EXPECT_EQ((*esp)->pins.size(), 38);
  for (auto header : {"LEFT", "RIGHT"})
    for (uint32_t n = 1; n <= 24; ++n) EXPECT_TRUE((*teensy)->FindPin(n, header).ok());
  for (auto header : {"J2", "J3"})
    for (uint32_t n = 1; n <= 19; ++n) EXPECT_TRUE((*esp)->FindPin(n, header).ok());
  auto input = (*esp)->FindPin(3, "J2");
  ASSERT_TRUE(input.ok());
  EXPECT_TRUE((*input)->Supports(PinFunction::kDigitalInput));
  EXPECT_FALSE((*input)->Supports(PinFunction::kDigitalOutput));
}
}  // namespace
}  // namespace robot::board
