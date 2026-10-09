// Pins version-neutral command values independently of either frame codec.
// Including only the semantic header verifies it has no frame/session dependency.
#include "firmware/common/joshua_wire_commands.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "gtest/gtest.h"

TEST(JoshuaWireCommands, WireIdsStayStable) {
  EXPECT_EQ(JW_CMD_IDENTIFY, 0x01);
  EXPECT_EQ(JW_CMD_CONFIGURE_CHANNEL, 0x02);
  EXPECT_EQ(JW_CMD_SET_TARGET, 0x03);
  EXPECT_EQ(JW_CMD_GET_FEEDBACK, 0x04);
  EXPECT_EQ(JW_CMD_ENABLE, 0x05);
  EXPECT_EQ(JW_CMD_DISABLE, 0x06);
  EXPECT_EQ(JW_CMD_ESTOP, 0x07);
  EXPECT_EQ(JW_CMD_RESET_SESSION, 0x08);
  EXPECT_EQ(JW_CHANNEL_NONE, 0xff);
  EXPECT_EQ(JW_MODE_INVALID, 0);
  EXPECT_EQ(JW_MODE_POSITION, 1);
  EXPECT_EQ(JW_MODE_VELOCITY, 2);
  EXPECT_EQ(JW_MODE_TORQUE, 3);
  EXPECT_EQ(JW_STATUS_INVALID, 0);
  EXPECT_EQ(JW_STATUS_OK, 1);
  EXPECT_EQ(JW_STATUS_ERROR, 2);
  EXPECT_EQ(JW_STATUS_UNSUPPORTED, 3);
  EXPECT_EQ(JW_BOARD_INVALID, 0);
  EXPECT_EQ(JW_BOARD_AM243, 1);
  EXPECT_EQ(JW_BOARD_TEENSY41, 2);
  EXPECT_EQ(JW_BOARD_ARDUINO_UNO, 3);
  EXPECT_EQ(JW_BOARD_ESP32, 8);  // Historical wire ID, not protobuf ESP32=7.
  EXPECT_EQ(JW_DRIVE_INVALID, 0);
  EXPECT_EQ(JW_DRIVE_STEP_DIR, 1);
  EXPECT_EQ(JW_DRIVE_PWM_DC, 2);
  EXPECT_EQ(JW_DRIVE_SERVO_BUS_UART, 3);
  EXPECT_EQ(JW_DRIVE_CAN, 4);
  EXPECT_EQ(JW_DRIVE_PDO_JOINT, 5);
}

TEST(JoshuaWireCommands, PayloadLimitsStayStable) {
  EXPECT_EQ(JW_MAX_CHANNELS, 8);
  EXPECT_EQ(JW_FW_NAME_LEN, 16);
  EXPECT_EQ(JW_IDENTIFY_RESPONSE_PAYLOAD_LEN, 26);
  EXPECT_EQ(JW_FEEDBACK_RESPONSE_PAYLOAD_LEN, 10);
  EXPECT_EQ(JW_STATUS_RESPONSE_PAYLOAD_LEN, 1);
  EXPECT_EQ(JW_SET_TARGET_PAYLOAD_LEN, 5);
  EXPECT_EQ(JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN, 11);
  jw_identify_response_t identity{};
  EXPECT_EQ(sizeof(identity.fw_name), JW_FW_NAME_LEN);
  EXPECT_EQ(sizeof(identity.channel_drives) / sizeof(identity.channel_drives[0]), JW_MAX_CHANNELS);
}

namespace {
using Bytes = std::vector<uint8_t>;

// Every payload codec enforces the same null/bounds contract without depending
// on either frame codec. Too-small output buffers must remain untouched.
template <typename Encode, typename Decode>
void CheckBounds(size_t size, Encode encode, Decode decode) {
  Bytes bytes(size + 1, 0xa5);
  EXPECT_EQ(encode(nullptr, size), -1);
  for (size_t cap = 0; cap < size; ++cap) {
    EXPECT_EQ(encode(bytes.data(), cap), -1);
    EXPECT_EQ(bytes, Bytes(size + 1, 0xa5));
  }
  ASSERT_EQ(encode(bytes.data(), size), size);
  EXPECT_EQ(bytes.back(), 0xa5);
  EXPECT_EQ(decode(nullptr, size), JW_RESULT_ERROR);
  for (size_t len = 0; len <= size + 1; ++len) {
    EXPECT_EQ(decode(bytes.data(), len), len == size ? JW_RESULT_OK : JW_RESULT_ERROR);
  }
}
}  // namespace

TEST(JoshuaWireCommands, PayloadGoldenBytesAndRoundTrips) {
  uint8_t bytes[JW_IDENTIFY_RESPONSE_PAYLOAD_LEN];
  ASSERT_EQ(jw_encode_set_target_payload(bytes, sizeof(bytes), JW_MODE_VELOCITY, -12.5f), 5);
  EXPECT_EQ(Bytes(bytes, bytes + 5), (Bytes{2, 0, 0, 0x48, 0xc1}));
  jw_set_target_t target{};
  ASSERT_EQ(jw_decode_set_target_payload(bytes, 5, &target), JW_RESULT_OK);
  EXPECT_EQ(target.mode, JW_MODE_VELOCITY);
  EXPECT_FLOAT_EQ(target.value, -12.5f);

  jw_feedback_t feedback{1.0f, -2.0f, 0xabcd};
  ASSERT_EQ(jw_encode_feedback_payload(bytes, sizeof(bytes), &feedback), 10);
  EXPECT_EQ(Bytes(bytes, bytes + 10), (Bytes{0, 0, 0x80, 0x3f, 0, 0, 0, 0xc0, 0xcd, 0xab}));
  jw_feedback_t decoded_feedback{};
  ASSERT_EQ(jw_decode_feedback_payload(bytes, 10, &decoded_feedback), JW_RESULT_OK);
  EXPECT_FLOAT_EQ(decoded_feedback.position, feedback.position);
  EXPECT_FLOAT_EQ(decoded_feedback.velocity, feedback.velocity);
  EXPECT_EQ(decoded_feedback.fault_flags, feedback.fault_flags);

  jw_configure_step_dir_t config{0x12345678, 2, 3, 4, 5, 6, 0x2345};
  ASSERT_EQ(jw_encode_configure_step_dir_payload(bytes, sizeof(bytes), &config), 11);
  EXPECT_EQ(Bytes(bytes, bytes + 11), (Bytes{0x78, 0x56, 0x34, 0x12, 1, 1, 4, 5, 6, 0x45, 0x23}));
  jw_configure_step_dir_t decoded_config{};
  ASSERT_EQ(jw_decode_configure_step_dir_payload(bytes, 11, &decoded_config), JW_RESULT_OK);
  EXPECT_EQ(decoded_config.max_pulse_rate_hz, config.max_pulse_rate_hz);
  EXPECT_EQ(decoded_config.invert_dir, 1);
  EXPECT_EQ(decoded_config.enable_active_low, 1);
  EXPECT_EQ(decoded_config.step_pin, 4);
  EXPECT_EQ(decoded_config.dir_pin, 5);
  EXPECT_EQ(decoded_config.enable_pin, 6);
  EXPECT_EQ(decoded_config.step_pulse_width_us, config.step_pulse_width_us);

  ASSERT_EQ(jw_encode_status_payload(JW_STATUS_UNSUPPORTED, bytes, sizeof(bytes)), 1);
  EXPECT_EQ(bytes[0], 3);
  jw_status_t status;
  ASSERT_EQ(jw_decode_status_payload(bytes, 1, &status), JW_RESULT_OK);
  EXPECT_EQ(status, JW_STATUS_UNSUPPORTED);
}

TEST(JoshuaWireCommands, IdentifyPreservesFullNameAndClearsUnusedDrives) {
  jw_identify_response_t identity{};
  identity.board_id = JW_BOARD_ESP32;
  memcpy(identity.fw_name, "0123456789abcdef", JW_FW_NAME_LEN);
  identity.n_channels = 2;
  std::fill(std::begin(identity.channel_drives), std::end(identity.channel_drives), JW_DRIVE_CAN);
  identity.channel_drives[0] = JW_DRIVE_STEP_DIR;
  identity.channel_drives[1] = JW_DRIVE_PWM_DC;
  uint8_t bytes[JW_IDENTIFY_RESPONSE_PAYLOAD_LEN];
  ASSERT_EQ(jw_encode_identify_payload(bytes, sizeof(bytes), &identity), sizeof(bytes));
  EXPECT_EQ(Bytes(bytes, bytes + sizeof(bytes)),
            (Bytes{8,   '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
                   'c', 'd', 'e', 'f', 2,   1,   2,   0,   0,   0,   0,   0,   0}));
  jw_identify_response_t decoded = identity;
  ASSERT_EQ(jw_decode_identify_payload(bytes, sizeof(bytes), &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.board_id, identity.board_id);
  EXPECT_EQ(memcmp(decoded.fw_name, identity.fw_name, JW_FW_NAME_LEN), 0);
  EXPECT_EQ(decoded.n_channels, 2);
  EXPECT_EQ(decoded.channel_drives[0], JW_DRIVE_STEP_DIR);
  EXPECT_EQ(decoded.channel_drives[1], JW_DRIVE_PWM_DC);
  for (int i = 2; i < JW_MAX_CHANNELS; ++i) EXPECT_EQ(decoded.channel_drives[i], JW_DRIVE_INVALID);
  identity.n_channels = JW_MAX_CHANNELS + 1;
  EXPECT_EQ(jw_encode_identify_payload(bytes, sizeof(bytes), &identity), -1);
  bytes[1 + JW_FW_NAME_LEN] = JW_MAX_CHANNELS + 1;
  EXPECT_EQ(jw_decode_identify_payload(bytes, sizeof(bytes), &decoded), JW_RESULT_ERROR);
}

TEST(JoshuaWireCommands, PayloadCodecsRejectNullAndWrongSize) {
  jw_identify_response_t identity{};
  jw_feedback_t feedback{};
  jw_configure_step_dir_t config{};
  jw_set_target_t target{};
  jw_status_t status;
  CheckBounds(
      JW_IDENTIFY_RESPONSE_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_identify_payload(p, n, &identity); },
      [&](const uint8_t* p, size_t n) { return jw_decode_identify_payload(p, n, &identity); });
  CheckBounds(
      JW_FEEDBACK_RESPONSE_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_feedback_payload(p, n, &feedback); },
      [&](const uint8_t* p, size_t n) { return jw_decode_feedback_payload(p, n, &feedback); });
  CheckBounds(
      JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_configure_step_dir_payload(p, n, &config); },
      [&](const uint8_t* p, size_t n) {
        return jw_decode_configure_step_dir_payload(p, n, &config);
      });
  CheckBounds(
      JW_SET_TARGET_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_set_target_payload(p, n, JW_MODE_POSITION, 1); },
      [&](const uint8_t* p, size_t n) { return jw_decode_set_target_payload(p, n, &target); });
  CheckBounds(
      JW_STATUS_RESPONSE_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_status_payload(JW_STATUS_OK, p, n); },
      [&](const uint8_t* p, size_t n) { return jw_decode_status_payload(p, n, &status); });
  uint8_t bytes[JW_IDENTIFY_RESPONSE_PAYLOAD_LEN] = {};
  EXPECT_EQ(jw_encode_identify_payload(bytes, sizeof(bytes), nullptr), -1);
  EXPECT_EQ(jw_encode_feedback_payload(bytes, sizeof(bytes), nullptr), -1);
  EXPECT_EQ(jw_encode_configure_step_dir_payload(bytes, sizeof(bytes), nullptr), -1);
  EXPECT_EQ(jw_decode_identify_payload(bytes, JW_IDENTIFY_RESPONSE_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_feedback_payload(bytes, JW_FEEDBACK_RESPONSE_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_configure_step_dir_payload(bytes, JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_set_target_payload(bytes, JW_SET_TARGET_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_status_payload(bytes, JW_STATUS_RESPONSE_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
}
