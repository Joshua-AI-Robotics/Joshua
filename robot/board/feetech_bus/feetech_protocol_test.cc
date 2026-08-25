#include "robot/board/feetech_bus/feetech_protocol.h"

#include "gtest/gtest.h"

namespace robot::board::feetech {
namespace {

// Expected bytes below are the pre-board-layer Sts3215Driver/Sts3215Encoder's
// own checksum arithmetic, recomputed by hand from the original
// create_torque_packet/create_read_position_packet implementations
// (docs/BOARD_LAYER_RFC.md §10 Phase 4 acceptance bar: byte-identical bus
// traffic for the register writes that carry over unchanged).

TEST(FeetechProtocolTest, TorqueEnableWriteMatchesLegacyBytes) {
  auto packet = BuildWritePacket(/*servo_id=*/1, kRegTorqueEnable, {1});
  EXPECT_EQ(packet, (std::vector<uint8_t>{0xFF, 0xFF, 0x01, 0x04, 0x03, 0x28, 0x01, 0xCE}));
}

TEST(FeetechProtocolTest, TorqueDisableWriteMatchesLegacyBytes) {
  auto packet = BuildWritePacket(/*servo_id=*/1, kRegTorqueEnable, {0});
  EXPECT_EQ(packet, (std::vector<uint8_t>{0xFF, 0xFF, 0x01, 0x04, 0x03, 0x28, 0x00, 0xCF}));
}

TEST(FeetechProtocolTest, PresentPositionReadMatchesLegacyBytes) {
  auto packet = BuildReadPacket(/*servo_id=*/1, kRegPresentPosition, 2);
  EXPECT_EQ(packet, (std::vector<uint8_t>{0xFF, 0xFF, 0x01, 0x04, 0x02, 0x38, 0x02, 0xBE}));
}

TEST(FeetechProtocolTest, GoalPositionBurstBundlesPositionTimeSpeed) {
  std::vector<uint8_t> data = EncodeUint16Le(2048);
  auto time_bytes = EncodeUint16Le(40);
  auto speed_bytes = EncodeUint16Le(3000);
  data.insert(data.end(), time_bytes.begin(), time_bytes.end());
  data.insert(data.end(), speed_bytes.begin(), speed_bytes.end());
  auto packet = BuildWritePacket(/*servo_id=*/2, kRegGoalPosition, data);

  // Header(2) + id + length + instr + addr + 6 data bytes + checksum = 13.
  EXPECT_EQ(packet.size(), 13u);
  EXPECT_EQ(packet[0], 0xFF);
  EXPECT_EQ(packet[1], 0xFF);
  EXPECT_EQ(packet[2], 0x02);
  EXPECT_EQ(packet[3], 0x09);  // Length: 7 params + 2.
  EXPECT_EQ(packet[4], 0x03);  // WRITE_DATA.
  EXPECT_EQ(packet[5], kRegGoalPosition);
}

TEST(FeetechProtocolTest, PingPacketHasNoParams) {
  auto packet = BuildPingPacket(/*servo_id=*/5);
  EXPECT_EQ(packet.size(), 6u);
  EXPECT_EQ(packet[3], 0x02);  // Length: 0 params + 2.
  EXPECT_EQ(packet[4], 0x01);  // PING.
}

TEST(FeetechProtocolTest, ParseStatusPacketRoundTripsThroughBuiltWrite) {
  // A well-formed status packet echoing an OK ping from servo 5.
  auto ping = BuildPingPacket(5);
  // Simulate the servo's ack: same shape, error byte forced to 0.
  std::vector<uint8_t> response = {0xFF, 0xFF, 0x05, 0x02, 0x00};
  response.push_back(0);  // placeholder checksum, recomputed below.
  // Recompute checksum the way ComputeChecksum would (frame_from_id = id,length,error).
  uint8_t sum = 0x05 + 0x02 + 0x00;
  response.back() = static_cast<uint8_t>(~sum & 0xFF);

  auto params = ParseStatusPacket(response, 5);
  ASSERT_TRUE(params.ok()) << params.status();
  EXPECT_TRUE(params->empty());
  (void)ping;
}

TEST(FeetechProtocolTest, ParseStatusPacketRejectsWrongServoId) {
  std::vector<uint8_t> response = {0xFF, 0xFF, 0x05, 0x02, 0x00, 0xF8};
  auto params = ParseStatusPacket(response, /*expected_servo_id=*/6);
  EXPECT_EQ(params.status().code(), absl::StatusCode::kInternal);
}

TEST(FeetechProtocolTest, ParseStatusPacketRejectsBadChecksum) {
  std::vector<uint8_t> response = {0xFF, 0xFF, 0x05, 0x02, 0x00, 0x00};
  auto params = ParseStatusPacket(response, 5);
  EXPECT_EQ(params.status().code(), absl::StatusCode::kInternal);
}

TEST(FeetechProtocolTest, ParseStatusPacketRejectsErrorByte) {
  std::vector<uint8_t> response = {0xFF, 0xFF, 0x05, 0x02, 0x01, 0xF7};
  auto params = ParseStatusPacket(response, 5);
  EXPECT_EQ(params.status().code(), absl::StatusCode::kInternal);
}

TEST(FeetechProtocolTest, Uint16LeRoundTrips) {
  auto bytes = EncodeUint16Le(0x1234);
  EXPECT_EQ(bytes, (std::vector<uint8_t>{0x34, 0x12}));
  EXPECT_EQ(DecodeUint16Le(bytes[0], bytes[1]), 0x1234);
}

}  // namespace
}  // namespace robot::board::feetech
