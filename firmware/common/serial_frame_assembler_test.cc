#include "firmware/common/serial_frame_assembler.h"

#include <vector>

#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;

Bytes Request() {
  Bytes frame(JW_MAX_FRAME_LEN);
  frame.resize(
      jw_encode_frame(frame.data(), frame.size(), 10, 1, JW_CMD_IDENTIFY, 0xff, nullptr, 0));
  return frame;
}

TEST(SerialFrameAssemblerTest, PartialAndConsecutiveFramesWithSyncInsidePayload) {
  serial_frame_assembler_t assembler;
  serial_frame_assembler_init(&assembler, 20);
  uint8_t out[JW_MAX_FRAME_LEN];
  size_t length = 0;
  Bytes frame(JW_MAX_FRAME_LEN);
  const uint8_t payload[] = {JW_SYNC_BYTE, 0xff, 0};
  frame.resize(jw_encode_frame(frame.data(), frame.size(), 10, 1, 1, 0, payload, sizeof(payload)));
  for (int repeat = 0; repeat < 2; ++repeat) {
    for (size_t i = 0; i < frame.size(); ++i) {
      const auto status =
          serial_frame_assembler_push(&assembler, i, frame[i], out, sizeof(out), &length);
      EXPECT_EQ(status, i + 1 == frame.size() ? FRAME_OK : FRAME_NO_DATA);
      EXPECT_EQ(length, i + 1 == frame.size() ? frame.size() : 0);
    }
    EXPECT_EQ(Bytes(out, out + length), frame);
  }
}

TEST(SerialFrameAssemblerTest, RejectsInvalidLengthAndRecoversAfterWrappingTimeout) {
  serial_frame_assembler_t assembler;
  serial_frame_assembler_init(&assembler, 20);
  uint8_t out[JW_MAX_FRAME_LEN];
  size_t length;
  EXPECT_EQ(serial_frame_assembler_push(&assembler, 0, 0, out, sizeof(out), &length),
            FRAME_NO_DATA);
  EXPECT_EQ(serial_frame_assembler_push(&assembler, 0, JW_SYNC_BYTE, out, sizeof(out), &length),
            FRAME_NO_DATA);
  EXPECT_EQ(serial_frame_assembler_push(&assembler, 0, 0xff, out, sizeof(out), &length),
            FRAME_ERROR);
  const auto frame = Request();
  for (size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(serial_frame_assembler_push(
                  &assembler, UINT32_MAX - 10, frame[i], out, sizeof(out), &length),
              FRAME_NO_DATA);
  }
  for (size_t i = 0; i < frame.size(); ++i) {
    const auto status =
        serial_frame_assembler_push(&assembler, 15, frame[i], out, sizeof(out), &length);
    EXPECT_EQ(status, i + 1 == frame.size() ? FRAME_OK : FRAME_NO_DATA);
  }
  EXPECT_EQ(Bytes(out, out + length), frame);
}

TEST(SerialFrameAssemblerTest, LeavesCrcValidationToEndpointAndRejectsSmallStorage) {
  serial_frame_assembler_t assembler;
  serial_frame_assembler_init(&assembler, 20);
  uint8_t out[JW_MAX_FRAME_LEN];
  size_t length = 99;
  EXPECT_EQ(serial_frame_assembler_push(&assembler, 0, JW_SYNC_BYTE, out, sizeof(out) - 1, &length),
            FRAME_ERROR);
  EXPECT_EQ(length, 0);
  auto frame = Request();
  frame.back() ^= 1;
  frame_status_t status = FRAME_NO_DATA;
  for (auto byte : frame)
    status = serial_frame_assembler_push(&assembler, 0, byte, out, sizeof(out), &length);
  EXPECT_EQ(status, FRAME_OK);
  jw_frame_t decoded;
  EXPECT_LT(jw_decode_frame(out, length, &decoded), JW_RESULT_OK);
}
}  // namespace
