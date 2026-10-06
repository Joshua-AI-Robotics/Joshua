#include "robot/board/joshua_wire/joshua_wire_v1_framer.h"

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "firmware/common/joshua_wire_v1.h"
#include "gtest/gtest.h"

namespace robot::board {
namespace {

std::vector<uint8_t> StatusFrame() {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_status_response(buf, sizeof(buf), JW1_CMD_ENABLE, 0, JW1_STATUS_OK);
  return std::vector<uint8_t>(buf, buf + len);
}

std::vector<uint8_t> IdentifyFrame() {
  jw1_identify_response_t response{};
  response.board_id = JW1_BOARD_TEENSY41;
  response.n_channels = 1;
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_identify_response(buf, sizeof(buf), &response);
  return std::vector<uint8_t>(buf, buf + len);
}

// Feeds `frame` to the framer the way a serial link would: a header read, then
// the remainder it asks for.
size_t FramedLength(const std::vector<uint8_t>& frame) {
  JoshuaWireV1Framer framer;
  std::vector<uint8_t> received;
  while (true) {
    auto remaining = framer.RemainingBytes(received);
    EXPECT_TRUE(remaining.ok()) << remaining.status();
    if (!remaining.ok() || *remaining == 0) return received.size();
    received.insert(received.end(),
                    frame.begin() + received.size(),
                    frame.begin() + received.size() + *remaining);
  }
}

TEST(JoshuaWireV1FramerTest, AsksForTheHeaderFirst) {
  JoshuaWireV1Framer framer;

  auto remaining = framer.RemainingBytes({});

  ASSERT_TRUE(remaining.ok());
  EXPECT_EQ(*remaining, 2u);
}

TEST(JoshuaWireV1FramerTest, DelimitsEveryFixedResponseSizeExactly) {
  EXPECT_EQ(FramedLength(StatusFrame()), JW1_FRAME_LEN(JW1_STATUS_RESPONSE_PAYLOAD_LEN));
  EXPECT_EQ(FramedLength(IdentifyFrame()), JW1_FRAME_LEN(JW1_IDENTIFY_RESPONSE_PAYLOAD_LEN));
}

TEST(JoshuaWireV1FramerTest, RejectsMissingSyncByte) {
  JoshuaWireV1Framer framer;
  const std::vector<uint8_t> received = {0x00};

  EXPECT_EQ(framer.RemainingBytes(received).status().code(), absl::StatusCode::kDataLoss);
}

TEST(JoshuaWireV1FramerTest, RejectsLengthOutsideTheProtocolBounds) {
  JoshuaWireV1Framer framer;
  const std::vector<uint8_t> too_short = {JW1_SYNC_BYTE, 2};
  const std::vector<uint8_t> too_long = {JW1_SYNC_BYTE, 3 + JW1_MAX_PAYLOAD_LEN + 1};

  EXPECT_EQ(framer.RemainingBytes(too_short).status().code(), absl::StatusCode::kDataLoss);
  EXPECT_EQ(framer.RemainingBytes(too_long).status().code(), absl::StatusCode::kDataLoss);
}

}  // namespace
}  // namespace robot::board
