#include "firmware/common/joshua_wire_v1.h"

#include <cstring>
#include <vector>

#include "gtest/gtest.h"

namespace {

// Expected bytes below are computed independently in Python (CRC-16/
// CCITT-FALSE, poly 0x1021, init 0xFFFF), not derived from this codec, so
// these tests catch a wrong-but-self-consistent encoder bug that a pure
// round-trip (decode(encode(x)) == x) would miss.

TEST(JoshuaWireV1Test, EncodeEnableMatchesGoldenBytes) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_enable(buf, sizeof(buf), 2);
  ASSERT_EQ(len, 7);
  const std::vector<uint8_t> expected = {0xa5, 0x03, 0x01, 0x05, 0x02, 0x1b, 0x24};
  EXPECT_EQ(std::vector<uint8_t>(buf, buf + len), expected);
}

TEST(JoshuaWireV1Test, EncodeSetTargetMatchesGoldenBytes) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_set_target(buf, sizeof(buf), 3, JW1_MODE_POSITION, 90.0f);
  ASSERT_EQ(len, 12);
  const std::vector<uint8_t> expected = {
      0xa5, 0x08, 0x01, 0x03, 0x03, 0x00, 0x00, 0x00, 0xb4, 0x42, 0xc0, 0xda};
  EXPECT_EQ(std::vector<uint8_t>(buf, buf + len), expected);
}

TEST(JoshuaWireV1Test, EncodeIdentifyRequestMatchesGoldenBytes) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_identify_request(buf, sizeof(buf));
  ASSERT_EQ(len, 7);
  const std::vector<uint8_t> expected = {0xa5, 0x03, 0x01, 0x01, 0xff, 0x6d, 0xd6};
  EXPECT_EQ(std::vector<uint8_t>(buf, buf + len), expected);
}

TEST(JoshuaWireV1Test, EncodeEstopMatchesGoldenBytes) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_estop(buf, sizeof(buf));
  ASSERT_EQ(len, 7);
  const std::vector<uint8_t> expected = {0xa5, 0x03, 0x01, 0x07, 0xff, 0xcb, 0x7c};
  EXPECT_EQ(std::vector<uint8_t>(buf, buf + len), expected);
}

TEST(JoshuaWireV1Test, EncodeFeedbackResponseMatchesGoldenBytes) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  jw1_feedback_t feedback{};
  feedback.position = 45.5f;
  feedback.velocity = -2.25f;
  feedback.fault_flags = 0x0007;
  const int len = jw1_encode_feedback_response(buf, sizeof(buf), 1, &feedback);
  ASSERT_EQ(len, 17);
  const std::vector<uint8_t> expected = {0xa5,
                                         0x0d,
                                         0x01,
                                         0x04,
                                         0x01,
                                         0x00,
                                         0x00,
                                         0x36,
                                         0x42,
                                         0x00,
                                         0x00,
                                         0x10,
                                         0xc0,
                                         0x07,
                                         0x00,
                                         0x2d,
                                         0x67};
  EXPECT_EQ(std::vector<uint8_t>(buf, buf + len), expected);
}

TEST(JoshuaWireV1Test, EncodeConfigureChannelStepDirMatchesGoldenBytes) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  jw1_configure_step_dir_t config{};
  config.max_pulse_rate_hz = 20000;
  config.invert_dir = 1;
  config.enable_active_low = 0;
  config.step_pin = 2;
  config.dir_pin = 3;
  config.enable_pin = 4;
  config.step_pulse_width_us = 500;
  const int len = jw1_encode_configure_channel_step_dir(buf, sizeof(buf), 0, &config);
  ASSERT_EQ(len, 18);
  const std::vector<uint8_t> expected = {0xa5,
                                         0x0e,
                                         0x01,
                                         0x02,
                                         0x00,
                                         0x20,
                                         0x4e,
                                         0x00,
                                         0x00,
                                         0x01,
                                         0x00,
                                         0x02,
                                         0x03,
                                         0x04,
                                         0xf4,
                                         0x01,
                                         0x83,
                                         0xcd};
  EXPECT_EQ(std::vector<uint8_t>(buf, buf + len), expected);
}

TEST(JoshuaWireV1Test, DecodeRejectsCorruptedCrc) {
  // Same bytes as EncodeSetTargetMatchesGoldenBytes with one payload byte
  // flipped, independently computed in Python (not round-tripped).
  uint8_t buf[] = {0xa5, 0x08, 0x01, 0x03, 0x03, 0xff, 0x00, 0x00, 0xb4, 0x42, 0xc0, 0xda};
  jw1_frame_t frame;
  EXPECT_EQ(jw1_decode_frame(buf, sizeof(buf), &frame), -1);
}

TEST(JoshuaWireV1Test, DecodeRejectsBadSyncByte) {
  uint8_t buf[] = {0x00, 0x03, 0x01, 0x05, 0x02, 0x1b, 0x24};
  jw1_frame_t frame;
  EXPECT_EQ(jw1_decode_frame(buf, sizeof(buf), &frame), -1);
}

TEST(JoshuaWireV1Test, DecodeRejectsTruncatedFrame) {
  uint8_t buf[] = {0xa5, 0x08, 0x01, 0x03, 0x03, 0x00, 0x00};
  jw1_frame_t frame;
  EXPECT_EQ(jw1_decode_frame(buf, sizeof(buf), &frame), -1);
}

TEST(JoshuaWireV1Test, EncodeDecodeRoundTripSetTarget) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_set_target(buf, sizeof(buf), 5, JW1_MODE_VELOCITY, -12.5f);
  ASSERT_GT(len, 0);

  jw1_frame_t frame;
  ASSERT_EQ(jw1_decode_frame(buf, static_cast<size_t>(len), &frame), 0);
  EXPECT_EQ(frame.proto_ver, JW1_PROTO_VERSION);
  EXPECT_EQ(frame.cmd, JW1_CMD_SET_TARGET);
  EXPECT_EQ(frame.channel, 5);

  jw1_set_target_t target;
  ASSERT_EQ(jw1_decode_set_target(&frame, &target), 0);
  EXPECT_EQ(target.mode, JW1_MODE_VELOCITY);
  EXPECT_FLOAT_EQ(target.value, -12.5f);
}

TEST(JoshuaWireV1Test, EncodeIdentifyResponseMatchesGoldenBytesAndPadsUnusedChannels) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  jw1_identify_response_t response{};
  response.board_id = JW1_BOARD_TEENSY41;
  std::memset(response.fw_name, 0, sizeof(response.fw_name));
  std::memcpy(response.fw_name, "teensy-stepdir", 14);
  response.n_channels = 2;
  response.channel_drives[0] = JW1_DRIVE_STEP_DIR;
  response.channel_drives[1] = JW1_DRIVE_STEP_DIR;

  const int len = jw1_encode_identify_response(buf, sizeof(buf), &response);
  // Fixed size regardless of n_channels — see JW1_IDENTIFY_RESPONSE_PAYLOAD_LEN.
  ASSERT_EQ(len, JW1_FRAME_LEN(JW1_IDENTIFY_RESPONSE_PAYLOAD_LEN));
  ASSERT_EQ(len, 33);
  const std::vector<uint8_t> expected = {0xa5, 0x1d, 0x01, 0x01, 0xff, 0x02, 0x74, 0x65, 0x65,
                                         0x6e, 0x73, 0x79, 0x2d, 0x73, 0x74, 0x65, 0x70, 0x64,
                                         0x69, 0x72, 0x00, 0x00, 0x02, 0x01, 0x01, 0x00, 0x00,
                                         0x00, 0x00, 0x00, 0x00, 0x4e, 0xaa};
  EXPECT_EQ(std::vector<uint8_t>(buf, buf + len), expected);

  jw1_frame_t frame;
  ASSERT_EQ(jw1_decode_frame(buf, static_cast<size_t>(len), &frame), 0);

  jw1_identify_response_t decoded;
  ASSERT_EQ(jw1_decode_identify_response(&frame, &decoded), 0);
  EXPECT_EQ(decoded.board_id, JW1_BOARD_TEENSY41);
  EXPECT_EQ(std::memcmp(decoded.fw_name, response.fw_name, JW1_FW_NAME_LEN), 0);
  EXPECT_EQ(decoded.n_channels, 2);
  EXPECT_EQ(decoded.channel_drives[0], JW1_DRIVE_STEP_DIR);
  EXPECT_EQ(decoded.channel_drives[1], JW1_DRIVE_STEP_DIR);
}

TEST(JoshuaWireV1Test, DecodeIdentifyResponseRejectsWrongPayloadLen) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len =
      jw1_encode_frame(buf, sizeof(buf), JW1_CMD_IDENTIFY, JW1_CHANNEL_NONE, nullptr, 0);
  ASSERT_GT(len, 0);
  jw1_frame_t frame;
  ASSERT_EQ(jw1_decode_frame(buf, static_cast<size_t>(len), &frame), 0);
  jw1_identify_response_t decoded;
  EXPECT_EQ(jw1_decode_identify_response(&frame, &decoded), -1);
}

TEST(JoshuaWireV1Test, EncodeFrameRejectsOversizedPayload) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  uint8_t payload[JW1_MAX_PAYLOAD_LEN + 1] = {0};
  EXPECT_EQ(jw1_encode_frame(buf, sizeof(buf), JW1_CMD_SET_TARGET, 0, payload, sizeof(payload)),
            -1);
}

TEST(JoshuaWireV1Test, EncodeFrameRejectsUndersizedBuffer) {
  uint8_t buf[3];
  EXPECT_EQ(jw1_encode_frame(buf, sizeof(buf), JW1_CMD_ENABLE, 0, nullptr, 0), -1);
}

TEST(JoshuaWireV1Test, EncodeFrameRejectsNullBuffer) {
  const uint8_t payload[1] = {0};
  EXPECT_EQ(jw1_encode_frame(nullptr, JW1_MAX_FRAME_LEN, JW1_CMD_ENABLE, 0, payload, 1), -1);
}

TEST(JoshuaWireV1Test, EncodeFrameRejectsNullPayloadWithNonzeroLen) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  EXPECT_EQ(jw1_encode_frame(buf, sizeof(buf), JW1_CMD_SET_TARGET, 0, nullptr, 5), -1);
}

TEST(JoshuaWireV1Test, DecodeFrameRejectsNullPointers) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  jw1_frame_t frame;
  EXPECT_EQ(jw1_decode_frame(nullptr, sizeof(buf), &frame), -1);
  EXPECT_EQ(jw1_decode_frame(buf, sizeof(buf), nullptr), -1);
}

TEST(JoshuaWireV1Test, EncodeConfigureChannelStepDirRejectsNullConfig) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  EXPECT_EQ(jw1_encode_configure_channel_step_dir(buf, sizeof(buf), 0, nullptr), -1);
}

TEST(JoshuaWireV1Test, EncodeIdentifyResponseRejectsNullResponse) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  EXPECT_EQ(jw1_encode_identify_response(buf, sizeof(buf), nullptr), -1);
}

TEST(JoshuaWireV1Test, EncodeFeedbackResponseRejectsNullFeedback) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  EXPECT_EQ(jw1_encode_feedback_response(buf, sizeof(buf), 0, nullptr), -1);
}

TEST(JoshuaWireV1Test, DecodeIdentifyResponseRejectsNullPointers) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len =
      jw1_encode_frame(buf, sizeof(buf), JW1_CMD_IDENTIFY, JW1_CHANNEL_NONE, nullptr, 0);
  ASSERT_GT(len, 0);
  jw1_frame_t frame;
  ASSERT_EQ(jw1_decode_frame(buf, static_cast<size_t>(len), &frame), 0);
  jw1_identify_response_t decoded;
  EXPECT_EQ(jw1_decode_identify_response(nullptr, &decoded), -1);
  EXPECT_EQ(jw1_decode_identify_response(&frame, nullptr), -1);
}

}  // namespace
