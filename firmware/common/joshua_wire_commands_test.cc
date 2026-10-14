// Pins version-neutral command values independently of either frame codec.
// Including only the semantic header verifies it has no frame/session dependency.
#include "firmware/common/joshua_wire_commands.h"

#include <cstring>
#include <utility>
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
  EXPECT_EQ(JW_CMD_READ_CHANNEL, 0x09);
  EXPECT_EQ(JW_CMD_WRITE_CHANNEL, 0x0a);
  EXPECT_EQ(JW_CHANNEL_NONE, 0xff);
  EXPECT_EQ(JW_MODE_INVALID, 0);
  EXPECT_EQ(JW_MODE_POSITION, 1);
  EXPECT_EQ(JW_MODE_VELOCITY, 2);
  EXPECT_EQ(JW_MODE_TORQUE, 3);
  EXPECT_EQ(JW_MODE_COMPLEX, 4);
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
  EXPECT_EQ(JW_CHANNEL_KIND_INVALID, 0);
  EXPECT_EQ(JW_CHANNEL_KIND_ACTUATOR, 1);
  EXPECT_EQ(JW_CHANNEL_KIND_SENSOR, 2);
  EXPECT_EQ(JW_CHANNEL_KIND_IO, 3);
  EXPECT_EQ(JW_CAP_CONFIGURE, 1u << 0);
  EXPECT_EQ(JW_CAP_READ, 1u << 1);
  EXPECT_EQ(JW_CAP_WRITE, 1u << 2);
  EXPECT_EQ(JW_CAP_STREAM, 1u << 3);
  EXPECT_EQ(JW_CAP_ENABLE_DISABLE, 1u << 4);
  EXPECT_EQ(JW_SENSOR_INTERFACE_SCALAR_F32, 1);
  EXPECT_EQ(JW_IO_INTERFACE_DIGITAL, 1);
  EXPECT_EQ(JW_IO_INTERFACE_ANALOG_F32, 2);
}

TEST(JoshuaWireCommands, PayloadLimitsStayStable) {
  EXPECT_EQ(JW_MAX_CHANNELS, 8);
  EXPECT_EQ(JW_FW_NAME_LEN, 16);
  EXPECT_EQ(JW_CHANNEL_DESCRIPTOR_WIRE_LEN, 7);
  EXPECT_EQ(JW_IDENTIFY_MIN_PAYLOAD_LEN, 18);
  EXPECT_EQ(JW_IDENTIFY_MAX_PAYLOAD_LEN, 74);
  EXPECT_EQ(JW_FEEDBACK_RESPONSE_PAYLOAD_LEN, 10);
  EXPECT_EQ(JW_STATUS_RESPONSE_PAYLOAD_LEN, 1);
  EXPECT_EQ(JW_SET_TARGET_PAYLOAD_LEN, 5);
  EXPECT_EQ(JW_SET_TARGET_COMPLEX_PAYLOAD_LEN, 13);
  EXPECT_EQ(JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN, 11);
  jw_identify_response_t identity{};
  EXPECT_EQ(sizeof(identity.fw_name), JW_FW_NAME_LEN);
  EXPECT_EQ(sizeof(identity.channels) / sizeof(identity.channels[0]), JW_MAX_CHANNELS);
}

namespace {
using Bytes = std::vector<uint8_t>;

jw_identify_response_t MixedIdentity() {
  jw_identify_response_t identity{};
  identity.board_id = JW_BOARD_ESP32;
  memcpy(identity.fw_name, "0123456789abcdef", JW_FW_NAME_LEN);
  identity.n_channels = 3;
  identity.channels[0] = {2,
                          JW_CHANNEL_KIND_ACTUATOR,
                          JW_CAP_CONFIGURE | JW_CAP_READ | JW_CAP_WRITE | JW_CAP_ENABLE_DISABLE,
                          {JW_DRIVE_STEP_DIR}};
  identity.channels[1].channel_id = 7;
  identity.channels[1].kind = JW_CHANNEL_KIND_SENSOR;
  identity.channels[1].capabilities = JW_CAP_READ | (1u << 31);
  identity.channels[1].interface.sensor_interface = JW_SENSOR_INTERFACE_SCALAR_F32;
  identity.channels[2].channel_id = 42;
  identity.channels[2].kind = JW_CHANNEL_KIND_IO;
  identity.channels[2].capabilities = JW_CAP_READ | JW_CAP_WRITE;
  identity.channels[2].interface.io_interface = JW_IO_INTERFACE_DIGITAL;
  return identity;
}

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
  uint8_t bytes[JW_IDENTIFY_MAX_PAYLOAD_LEN];
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

TEST(JoshuaWireCommands, ComplexTargetMatchesGoldenBytesAndPreservesAllFields) {
  const jw_complex_target_t target{1.0f, -2.0f, 0.5f};
  uint8_t bytes[JW_SET_TARGET_COMPLEX_PAYLOAD_LEN];
  ASSERT_EQ(jw_encode_complex_target_payload(bytes, sizeof(bytes), &target), sizeof(bytes));
  EXPECT_EQ(Bytes(bytes, bytes + sizeof(bytes)),
            (Bytes{4, 0, 0, 0x80, 0x3f, 0, 0, 0, 0xc0, 0, 0, 0, 0x3f}));

  jw_set_target_t decoded{};
  decoded.value = 99.0f;
  ASSERT_EQ(jw_decode_set_target_payload(bytes, sizeof(bytes), &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.mode, JW_MODE_COMPLEX);
  EXPECT_FLOAT_EQ(decoded.value, 0.0f);
  EXPECT_FLOAT_EQ(decoded.combined.position, target.position);
  EXPECT_FLOAT_EQ(decoded.combined.velocity, target.velocity);
  EXPECT_FLOAT_EQ(decoded.combined.torque, target.torque);

  // Reusing the decoded view for a scalar target must clear all combined fields.
  for (const auto mode : {JW_MODE_POSITION, JW_MODE_VELOCITY, JW_MODE_TORQUE}) {
    decoded.combined = target;
    ASSERT_EQ(jw_encode_set_target_payload(bytes, sizeof(bytes), mode, -12.5f), 5);
    ASSERT_EQ(jw_decode_set_target_payload(bytes, 5, &decoded), JW_RESULT_OK);
    EXPECT_EQ(decoded.mode, mode);
    EXPECT_FLOAT_EQ(decoded.value, -12.5f);
    EXPECT_FLOAT_EQ(decoded.combined.position, 0.0f);
    EXPECT_FLOAT_EQ(decoded.combined.velocity, 0.0f);
    EXPECT_FLOAT_EQ(decoded.combined.torque, 0.0f);
  }
}

TEST(JoshuaWireCommands, ComplexTargetRequiresItsFullLayoutAndPreservesOutputOnFailure) {
  Bytes bytes(JW_SET_TARGET_COMPLEX_PAYLOAD_LEN, 0xa5);
  EXPECT_EQ(jw_encode_set_target_payload(bytes.data(), bytes.size(), JW_MODE_COMPLEX, 1.0f), -1);
  EXPECT_EQ(bytes, Bytes(JW_SET_TARGET_COMPLEX_PAYLOAD_LEN, 0xa5));

  const jw_complex_target_t target{0.0f, 0.0f, 0.0f};
  ASSERT_EQ(jw_encode_complex_target_payload(bytes.data(), bytes.size(), &target), bytes.size());
  jw_set_target_t decoded{JW_MODE_VELOCITY, 12.5f, {1.0f, 2.0f, 3.0f}};
  for (size_t len = 0; len < bytes.size(); ++len) {
    EXPECT_EQ(jw_decode_set_target_payload(bytes.data(), len, &decoded), JW_RESULT_ERROR);
    EXPECT_EQ(decoded.mode, JW_MODE_VELOCITY);
    EXPECT_FLOAT_EQ(decoded.value, 12.5f);
    EXPECT_FLOAT_EQ(decoded.combined.position, 1.0f);
    EXPECT_FLOAT_EQ(decoded.combined.velocity, 2.0f);
    EXPECT_FLOAT_EQ(decoded.combined.torque, 3.0f);
  }
  ASSERT_EQ(jw_decode_set_target_payload(bytes.data(), bytes.size(), &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.mode, JW_MODE_COMPLEX);
  EXPECT_FLOAT_EQ(decoded.combined.position, 0.0f);
  EXPECT_FLOAT_EQ(decoded.combined.velocity, 0.0f);
  EXPECT_FLOAT_EQ(decoded.combined.torque, 0.0f);
  bytes[0] = JW_MODE_POSITION;
  EXPECT_EQ(jw_decode_set_target_payload(bytes.data(), bytes.size(), &decoded), JW_RESULT_ERROR);
}

TEST(JoshuaWireCommands, MixedIdentifyMatchesGoldenBytesAndClearsUnusedDescriptors) {
  auto identity = MixedIdentity();
  Bytes bytes(JW_IDENTIFY_MAX_PAYLOAD_LEN + 1, 0xa5);
  const int len = jw_encode_identify_payload(bytes.data(), bytes.size(), &identity);
  ASSERT_EQ(len, 39);
  EXPECT_EQ(Bytes(bytes.begin(), bytes.begin() + len),
            (Bytes{8,   '0', '1', '2', '3',  '4', '5', '6',  '7', '8', '9', 'a', 'b',
                   'c', 'd', 'e', 'f', 3,    2,   1,   0x17, 0,   0,   0,   1,   7,
                   2,   2,   0,   0,   0x80, 1,   42,  3,    6,   0,   0,   0,   1}));
  for (size_t i = len; i < bytes.size(); ++i) EXPECT_EQ(bytes[i], 0xa5);
  jw_identify_response_t decoded{};
  for (auto& channel : decoded.channels) {
    channel.channel_id = 99;
    channel.kind = JW_CHANNEL_KIND_IO;
    channel.capabilities = UINT32_MAX;
    channel.interface.io_interface = JW_IO_INTERFACE_ANALOG_F32;
  }
  ASSERT_EQ(jw_decode_identify_payload(bytes.data(), len, &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.board_id, identity.board_id);
  EXPECT_EQ(memcmp(decoded.fw_name, identity.fw_name, JW_FW_NAME_LEN), 0);
  EXPECT_EQ(decoded.n_channels, 3);
  EXPECT_EQ(decoded.channels[0].channel_id, 2);
  EXPECT_EQ(decoded.channels[0].kind, JW_CHANNEL_KIND_ACTUATOR);
  EXPECT_EQ(decoded.channels[0].interface.actuator_drive, JW_DRIVE_STEP_DIR);
  EXPECT_EQ(decoded.channels[1].channel_id, 7);
  EXPECT_EQ(decoded.channels[1].kind, JW_CHANNEL_KIND_SENSOR);
  EXPECT_EQ(decoded.channels[1].capabilities, JW_CAP_READ | (1u << 31));
  EXPECT_EQ(decoded.channels[1].interface.sensor_interface, JW_SENSOR_INTERFACE_SCALAR_F32);
  EXPECT_EQ(decoded.channels[2].channel_id, 42);
  EXPECT_EQ(decoded.channels[2].kind, JW_CHANNEL_KIND_IO);
  EXPECT_EQ(decoded.channels[2].interface.io_interface, JW_IO_INTERFACE_DIGITAL);
  for (int i = decoded.n_channels; i < JW_MAX_CHANNELS; ++i) {
    EXPECT_EQ(decoded.channels[i].channel_id, 0);
    EXPECT_EQ(decoded.channels[i].kind, JW_CHANNEL_KIND_INVALID);
    EXPECT_EQ(decoded.channels[i].capabilities, 0);
  }
}

TEST(JoshuaWireCommands, IdentifyLengthFollowsCountAndEnforcesBounds) {
  auto identity = MixedIdentity();
  for (uint8_t count = 0; count <= JW_MAX_CHANNELS; ++count) {
    identity.n_channels = count;
    for (uint8_t i = 0; i < count; ++i) {
      identity.channels[i] = {i, JW_CHANNEL_KIND_ACTUATOR, JW_CAP_READ, {JW_DRIVE_CAN}};
    }
    const size_t len = jw_identify_payload_len(count);
    EXPECT_EQ(len, 18 + 7 * count);
    jw_identify_response_t decoded{};
    CheckBounds(
        len,
        [&](uint8_t* p, size_t n) { return jw_encode_identify_payload(p, n, &identity); },
        [&](const uint8_t* p, size_t n) { return jw_decode_identify_payload(p, n, &decoded); });
    EXPECT_EQ(decoded.n_channels, count);
  }
  Bytes bytes(JW_IDENTIFY_MAX_PAYLOAD_LEN, 0xa5);
  identity.n_channels = JW_MAX_CHANNELS + 1;
  EXPECT_EQ(jw_identify_payload_len(identity.n_channels), 0);
  EXPECT_EQ(jw_encode_identify_payload(bytes.data(), bytes.size(), &identity), -1);
  EXPECT_EQ(bytes, Bytes(bytes.size(), 0xa5));
}

TEST(JoshuaWireCommands, IdentifyRejectsMalformedMetadataWithoutModifyingOutput) {
  const auto identity = MixedIdentity();
  Bytes bytes(jw_identify_payload_len(identity.n_channels));
  ASSERT_EQ(jw_encode_identify_payload(bytes.data(), bytes.size(), &identity), bytes.size());
  const size_t second = JW_IDENTIFY_CHANNELS_OFFSET + JW_CHANNEL_DESCRIPTOR_WIRE_LEN;
  const std::pair<size_t, uint8_t> mutations[] = {
      {JW_IDENTIFY_COUNT_OFFSET, JW_MAX_CHANNELS + 1},
      {JW_IDENTIFY_COUNT_OFFSET, 2},
      {second + JW_CHANNEL_ID_OFFSET, 2},
      {second + JW_CHANNEL_ID_OFFSET, JW_CHANNEL_NONE},
      {second + JW_CHANNEL_KIND_OFFSET, JW_CHANNEL_KIND_INVALID},
      {second + JW_CHANNEL_KIND_OFFSET, 99},
      {second + JW_CHANNEL_INTERFACE_OFFSET, 0},
      {second + JW_CHANNEL_INTERFACE_OFFSET, 200},
  };
  for (const auto& mutation : mutations) {
    auto malformed = bytes;
    malformed[mutation.first] = mutation.second;
    auto decoded = identity;
    EXPECT_EQ(jw_decode_identify_payload(malformed.data(), malformed.size(), &decoded),
              JW_RESULT_ERROR);
    EXPECT_EQ(decoded.n_channels, identity.n_channels);
    EXPECT_EQ(decoded.channels[1].channel_id, 7);
    EXPECT_EQ(decoded.channels[1].kind, JW_CHANNEL_KIND_SENSOR);
    EXPECT_EQ(decoded.channels[1].interface.sensor_interface, JW_SENSOR_INTERFACE_SCALAR_F32);
  }
  // Old IDENTIFY's drive-only payload must not be reinterpreted as descriptors.
  const Bytes legacy{8,   '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b',
                     'c', 'd', 'e', 'f', 2,   1,   2,   0,   0,   0,   0,   0,   0};
  auto decoded = identity;
  EXPECT_EQ(jw_decode_identify_payload(legacy.data(), legacy.size(), &decoded), JW_RESULT_ERROR);
  for (int invalid = 0; invalid < 5; ++invalid) {
    auto malformed = identity;
    switch (invalid) {
      case 0:
        malformed.channels[1].channel_id = 2;
        break;
      case 1:
        malformed.channels[1].channel_id = JW_CHANNEL_NONE;
        break;
      case 2:
        malformed.channels[1].kind = JW_CHANNEL_KIND_INVALID;
        break;
      case 3:
        malformed.channels[1].interface.sensor_interface = JW_SENSOR_INTERFACE_INVALID;
        break;
      case 4:
        malformed.channels[0].interface.actuator_drive = JW_DRIVE_INVALID;
        break;
    }
    Bytes output(bytes.size(), 0xa5);
    EXPECT_EQ(jw_encode_identify_payload(output.data(), output.size(), &malformed), -1);
    EXPECT_EQ(output, Bytes(output.size(), 0xa5));
  }
}

TEST(JoshuaWireCommands, CommandsRequireTheAdvertisedKindAndCapability) {
  auto identity = MixedIdentity();
  auto& actuator = identity.channels[0];
  auto& sensor = identity.channels[1];
  auto& io = identity.channels[2];
  EXPECT_TRUE(jw_channel_supports_command(&actuator, JW_CMD_CONFIGURE_CHANNEL));
  EXPECT_TRUE(jw_channel_supports_command(&actuator, JW_CMD_SET_TARGET));
  EXPECT_TRUE(jw_channel_supports_command(&actuator, JW_CMD_GET_FEEDBACK));
  EXPECT_TRUE(jw_channel_supports_command(&actuator, JW_CMD_ENABLE));
  EXPECT_TRUE(jw_channel_supports_command(&actuator, JW_CMD_DISABLE));
  EXPECT_FALSE(jw_channel_supports_command(&actuator, JW_CMD_READ_CHANNEL));
  EXPECT_FALSE(jw_channel_supports_command(&actuator, JW_CMD_WRITE_CHANNEL));
  EXPECT_TRUE(jw_channel_supports_command(&sensor, JW_CMD_READ_CHANNEL));
  EXPECT_TRUE(jw_channel_supports_command(&io, JW_CMD_READ_CHANNEL));
  EXPECT_TRUE(jw_channel_supports_command(&io, JW_CMD_WRITE_CHANNEL));
  sensor.capabilities = UINT32_MAX;
  io.capabilities = UINT32_MAX;
  for (const auto* channel : {&sensor, &io}) {
    EXPECT_FALSE(jw_channel_supports_command(channel, JW_CMD_SET_TARGET));
    EXPECT_FALSE(jw_channel_supports_command(channel, JW_CMD_GET_FEEDBACK));
    EXPECT_FALSE(jw_channel_supports_command(channel, JW_CMD_CONFIGURE_CHANNEL));
    EXPECT_FALSE(jw_channel_supports_command(channel, JW_CMD_IDENTIFY));
    EXPECT_FALSE(jw_channel_supports_command(channel, JW_CMD_RESET_SESSION));
    EXPECT_FALSE(jw_channel_supports_command(channel, JW_CMD_ESTOP));
  }
  EXPECT_FALSE(jw_channel_supports_command(&sensor, JW_CMD_WRITE_CHANNEL));
  actuator.capabilities = 0;
  sensor.capabilities = 0;
  io.capabilities = 0;
  EXPECT_FALSE(jw_channel_supports_command(&actuator, JW_CMD_SET_TARGET));
  EXPECT_FALSE(jw_channel_supports_command(&actuator, JW_CMD_GET_FEEDBACK));
  EXPECT_FALSE(jw_channel_supports_command(&actuator, JW_CMD_CONFIGURE_CHANNEL));
  EXPECT_FALSE(jw_channel_supports_command(&actuator, JW_CMD_ENABLE));
  EXPECT_FALSE(jw_channel_supports_command(&sensor, JW_CMD_READ_CHANNEL));
  EXPECT_FALSE(jw_channel_supports_command(&io, JW_CMD_WRITE_CHANNEL));
  EXPECT_FALSE(jw_channel_supports_command(nullptr, JW_CMD_READ_CHANNEL));
}

TEST(JoshuaWireCommands, SensorAndIoValuesHaveInterfaceSpecificGoldenBytes) {
  uint8_t bytes[4];
  const jw_sensor_sample_t sample{-12.5f};
  ASSERT_EQ(jw_encode_sensor_sample_payload(
                bytes, sizeof(bytes), JW_SENSOR_INTERFACE_SCALAR_F32, &sample),
            4);
  EXPECT_EQ(Bytes(bytes, bytes + 4), (Bytes{0, 0, 0x48, 0xc1}));
  jw_sensor_sample_t decoded{};
  ASSERT_EQ(jw_decode_sensor_sample_payload(bytes, 4, JW_SENSOR_INTERFACE_SCALAR_F32, &decoded),
            JW_RESULT_OK);
  EXPECT_FLOAT_EQ(decoded.value, -12.5f);
  jw_io_value_t io{};
  io.interface = JW_IO_INTERFACE_ANALOG_F32;
  io.value.analog = 1.0f;
  ASSERT_EQ(jw_encode_io_payload(bytes, sizeof(bytes), &io), 4);
  EXPECT_EQ(Bytes(bytes, bytes + 4), (Bytes{0, 0, 0x80, 0x3f}));
  jw_io_value_t decoded_io{};
  ASSERT_EQ(jw_decode_io_payload(bytes, 4, io.interface, &decoded_io), JW_RESULT_OK);
  EXPECT_EQ(decoded_io.interface, JW_IO_INTERFACE_ANALOG_F32);
  EXPECT_FLOAT_EQ(decoded_io.value.analog, 1.0f);
  io.interface = JW_IO_INTERFACE_DIGITAL;
  for (uint8_t value : {0, 1}) {
    io.value.digital = value;
    ASSERT_EQ(jw_encode_io_payload(bytes, sizeof(bytes), &io), 1);
    EXPECT_EQ(bytes[0], value);
    ASSERT_EQ(jw_decode_io_payload(bytes, 1, io.interface, &decoded_io), JW_RESULT_OK);
    EXPECT_EQ(decoded_io.interface, JW_IO_INTERFACE_DIGITAL);
    EXPECT_EQ(decoded_io.value.digital, value);
  }
  io.value.digital = 2;
  bytes[0] = 0xa5;
  EXPECT_EQ(jw_encode_io_payload(bytes, sizeof(bytes), &io), -1);
  EXPECT_EQ(bytes[0], 0xa5);
  EXPECT_EQ(jw_decode_io_payload(bytes, 1, JW_IO_INTERFACE_DIGITAL, &decoded_io), JW_RESULT_ERROR);
  EXPECT_EQ(decoded_io.value.digital, 1);
}

TEST(JoshuaWireCommands, PayloadCodecsRejectNullAndWrongSize) {
  jw_identify_response_t identity{};
  jw_feedback_t feedback{};
  jw_configure_step_dir_t config{};
  jw_set_target_t target{};
  jw_complex_target_t complex{};
  jw_status_t status;
  jw_sensor_sample_t sample{};
  jw_sensor_sample_t decoded_sample{};
  jw_io_value_t io{};
  jw_io_value_t decoded_io{};
  CheckBounds(
      JW_IDENTIFY_MIN_PAYLOAD_LEN,
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
      JW_SET_TARGET_COMPLEX_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_complex_target_payload(p, n, &complex); },
      [&](const uint8_t* p, size_t n) { return jw_decode_set_target_payload(p, n, &target); });
  CheckBounds(
      JW_STATUS_RESPONSE_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_status_payload(JW_STATUS_OK, p, n); },
      [&](const uint8_t* p, size_t n) { return jw_decode_status_payload(p, n, &status); });
  CheckBounds(
      JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) {
        return jw_encode_sensor_sample_payload(p, n, JW_SENSOR_INTERFACE_SCALAR_F32, &sample);
      },
      [&](const uint8_t* p, size_t n) {
        return jw_decode_sensor_sample_payload(
            p, n, JW_SENSOR_INTERFACE_SCALAR_F32, &decoded_sample);
      });
  for (const auto interface : {JW_IO_INTERFACE_DIGITAL, JW_IO_INTERFACE_ANALOG_F32}) {
    io.interface = interface;
    CheckBounds(
        interface == JW_IO_INTERFACE_DIGITAL ? 1 : 4,
        [&](uint8_t* p, size_t n) { return jw_encode_io_payload(p, n, &io); },
        [&](const uint8_t* p, size_t n) {
          return jw_decode_io_payload(p, n, interface, &decoded_io);
        });
  }
  uint8_t bytes[JW_IDENTIFY_MAX_PAYLOAD_LEN] = {};
  EXPECT_EQ(jw_encode_identify_payload(bytes, sizeof(bytes), nullptr), -1);
  EXPECT_EQ(jw_encode_feedback_payload(bytes, sizeof(bytes), nullptr), -1);
  EXPECT_EQ(jw_encode_configure_step_dir_payload(bytes, sizeof(bytes), nullptr), -1);
  EXPECT_EQ(jw_encode_complex_target_payload(bytes, sizeof(bytes), nullptr), -1);
  EXPECT_EQ(jw_encode_sensor_sample_payload(
                bytes, sizeof(bytes), JW_SENSOR_INTERFACE_SCALAR_F32, nullptr),
            -1);
  EXPECT_EQ(
      jw_encode_sensor_sample_payload(bytes, sizeof(bytes), JW_SENSOR_INTERFACE_INVALID, &sample),
      -1);
  EXPECT_EQ(jw_encode_io_payload(bytes, sizeof(bytes), nullptr), -1);
  io.interface = JW_IO_INTERFACE_INVALID;
  EXPECT_EQ(jw_encode_io_payload(bytes, sizeof(bytes), &io), -1);
  EXPECT_EQ(jw_decode_identify_payload(bytes, JW_IDENTIFY_MIN_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_feedback_payload(bytes, JW_FEEDBACK_RESPONSE_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_configure_step_dir_payload(bytes, JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_set_target_payload(bytes, JW_SET_TARGET_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_set_target_payload(bytes, JW_SET_TARGET_COMPLEX_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_status_payload(bytes, JW_STATUS_RESPONSE_PAYLOAD_LEN, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_sensor_sample_payload(bytes, 4, JW_SENSOR_INTERFACE_SCALAR_F32, nullptr),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_sensor_sample_payload(bytes, 4, JW_SENSOR_INTERFACE_INVALID, &sample),
            JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_io_payload(bytes, 1, JW_IO_INTERFACE_DIGITAL, nullptr), JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_io_payload(bytes, 1, JW_IO_INTERFACE_INVALID, &decoded_io), JW_RESULT_ERROR);
}
