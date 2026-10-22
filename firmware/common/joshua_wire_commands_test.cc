// Core codecs and versioned profiles are separate libraries, independently usable.
#include "firmware/common/joshua_wire_commands.h"

#include <cstring>
#include <limits>
#include <vector>

#include "firmware/common/joshua_wire_profiles.h"
#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;
const jw_measurement_metadata_t kMetadata{
    JW_SAMPLE_VALID, JW_CLOCK_BOOT_MONOTONIC_US, 0x12345678, 0x0102030405060708ULL, 9};
const Bytes kMetadataBytes{1, 1, 0x78, 0x56, 0x34, 0x12, 8, 7, 6, 5, 4, 3, 2, 1, 9, 0, 0, 0};
template <typename Encode, typename Decode>
void CheckBounds(size_t size, Encode encode, Decode decode, size_t minimum = 0) {
  Bytes bytes(size + 1, 0xa5);
  EXPECT_EQ(encode(nullptr, size), -1);
  for (size_t cap = 0; cap < size; ++cap) {
    EXPECT_EQ(encode(bytes.data(), cap), -1);
    EXPECT_EQ(bytes, Bytes(size + 1, 0xa5));
  }
  ASSERT_EQ(encode(bytes.data(), size), size);
  EXPECT_EQ(bytes.back(), 0xa5);
  EXPECT_EQ(decode(nullptr, size), JW_RESULT_ERROR);
  for (size_t len = 0; len <= size + 1; ++len)
    EXPECT_EQ(decode(bytes.data(), len),
              (len == size || (minimum != 0 && len >= minimum)) ? JW_RESULT_OK : JW_RESULT_ERROR);
}
}  // namespace

TEST(JoshuaWireCommands, CoreIdsAndLimits) {
  EXPECT_EQ(JW_CMD_IDENTIFY, 1);
  EXPECT_EQ(JW_CMD_ESTOP, 7);
  EXPECT_EQ(JW_CMD_RESET_SESSION, 8);
  EXPECT_EQ(JW_CMD_PROFILE, 11);
  EXPECT_EQ(JW_CHANNEL_NONE, 255);
  EXPECT_EQ(JW_MAX_FUNCTIONS, 255);
  EXPECT_EQ(JW_FUNCTION_DESCRIPTOR_LEN, 13);
  EXPECT_EQ(JW_PROFILE_HEADER_LEN, 5);
  EXPECT_LE(JW_DISCOVERY_MAX_LEN + JW_REPLY_HEADER_LEN, JW_PAYLOAD_LIMIT);
  EXPECT_LE(JW_DEPENDENCY_MAX_LEN + JW_REPLY_HEADER_LEN, JW_PAYLOAD_LIMIT);
}
TEST(JoshuaWireCommands, CommonRepliesDisambiguateErrorsFromData) {
  uint8_t bytes[JW_PAYLOAD_LIMIT]{};
  const uint8_t data[] = {JW_STATUS_UNAVAILABLE, 3, 4};
  ASSERT_EQ(jw_encode_reply_payload(
                bytes, sizeof(bytes), JW_STATUS_OK, JW_EXECUTION_COMPLETE, data, sizeof(data)),
            5);
  EXPECT_EQ(Bytes(bytes, bytes + 5), (Bytes{1, 2, 6, 3, 4}));
  jw_reply_t reply{};
  ASSERT_EQ(jw_decode_reply_payload(bytes, 5, &reply), JW_RESULT_OK);
  EXPECT_EQ(reply.status, JW_STATUS_OK);
  EXPECT_EQ(reply.data_len, 3);
  EXPECT_EQ(reply.data[0], JW_STATUS_UNAVAILABLE);
  CheckBounds(
      2,
      [&](uint8_t* p, size_t n) {
        return jw_encode_reply_payload(
            p, n, JW_STATUS_NOT_READY, JW_EXECUTION_REJECTED, nullptr, 0);
      },
      [&](const uint8_t* p, size_t n) { return jw_decode_reply_payload(p, n, &reply); },
      JW_REPLY_HEADER_LEN);
  EXPECT_EQ(jw_encode_reply_payload(
                bytes, sizeof(bytes), JW_STATUS_BUSY, JW_EXECUTION_ACCEPTED, nullptr, 0),
            -1);
  EXPECT_EQ(jw_encode_reply_payload(
                bytes, sizeof(bytes), JW_STATUS_OUTCOME_UNKNOWN, JW_EXECUTION_COMPLETE, nullptr, 0),
            -1);
  bytes[0] = 99;
  EXPECT_EQ(jw_decode_reply_payload(bytes, 2, &reply), JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_reply_payload(bytes, 2, nullptr), JW_RESULT_ERROR);
}
TEST(JoshuaWireCommands, IdentityHasNoBoardEnumOrInlineChannelLimit) {
  jw_identify_response_t v{};
  v.vendor_id = 0xfedcba98;
  v.product_id = 0x12345678;
  memcpy(v.fw_name, "0123456789abcdef", JW_FW_NAME_LEN);
  v.boot_id = 0x01020304;
  v.discovery_generation = 5;
  v.function_count = JW_MAX_FUNCTIONS;
  v.max_frame_len = 96;
  v.max_inflight = 1;
  v.max_page_entries = 4;
  v.max_handler_us = 100;
  v.safety_flags = JW_SAFETY_BOARD_ESTOP;
  v.protocol_min = v.protocol_max = 4;
  v.supported_planes = JW_PLANE_MESSAGE;
  jw_identify_response_t decoded{};
  CheckBounds(
      JW_IDENTIFY_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_identify_payload(p, n, &v); },
      [&](const uint8_t* p, size_t n) { return jw_decode_identify_payload(p, n, &decoded); });
  EXPECT_EQ(decoded.vendor_id, v.vendor_id);
  EXPECT_EQ(decoded.function_count, 255);
  EXPECT_EQ(decoded.boot_id, v.boot_id);
  EXPECT_EQ(memcmp(decoded.fw_name, v.fw_name, JW_FW_NAME_LEN), 0);
  Bytes bytes(JW_IDENTIFY_PAYLOAD_LEN);
  ASSERT_EQ(jw_encode_identify_payload(bytes.data(), bytes.size(), &v), bytes.size());
  EXPECT_EQ(Bytes(bytes.begin(), bytes.begin() + 8),
            (Bytes{0x98, 0xba, 0xdc, 0xfe, 0x78, 0x56, 0x34, 0x12}));
  bytes[36] = 2;  // Multiple in-flight requests are not supported by this session.
  EXPECT_EQ(jw_decode_identify_payload(bytes.data(), bytes.size(), &decoded), JW_RESULT_ERROR);
  EXPECT_EQ(decoded.max_inflight, 1);
}
TEST(JoshuaWireCommands, OpaqueProfileDescriptorsAndPagesHaveGoldenLayouts) {
  jw_function_descriptor_t f{42, 0x1234, 0xbeef, 7, 9, 0x80000003, 0x5678};
  uint8_t bytes[JW_FUNCTION_DESCRIPTOR_LEN];
  ASSERT_EQ(jw_encode_function_descriptor(bytes, sizeof(bytes), &f), sizeof(bytes));
  EXPECT_EQ(Bytes(bytes, bytes + sizeof(bytes)),
            (Bytes{42, 0x34, 0x12, 0xef, 0xbe, 7, 9, 3, 0, 0, 0x80, 0x78, 0x56}));
  jw_function_descriptor_t decoded{};
  CheckBounds(
      sizeof(bytes),
      [&](uint8_t* p, size_t n) { return jw_encode_function_descriptor(p, n, &f); },
      [&](const uint8_t* p, size_t n) { return jw_decode_function_descriptor(p, n, &decoded); });
  EXPECT_EQ(decoded.profile_id, 0xbeef);
  EXPECT_EQ(decoded.operations, 0x80000003);
  jw_discovery_page_t page{};
  page.generation = 1;
  page.total = 12;
  page.next_cursor = 4;
  page.count = 4;
  for (uint8_t i = 0; i < 4; ++i) {
    page.functions[i] = f;
    page.functions[i].function_id = i;
  }
  jw_discovery_page_t result{};
  CheckBounds(
      JW_DISCOVERY_MAX_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_discovery_page(p, n, &page); },
      [&](const uint8_t* p, size_t n) { return jw_decode_discovery_page(p, n, &result); });
  EXPECT_EQ(result.next_cursor, 4);
  page.functions[1].function_id = 0;
  Bytes output(JW_DISCOVERY_MAX_LEN, 0xa5);
  EXPECT_EQ(jw_encode_discovery_page(output.data(), output.size(), &page), -1);
  EXPECT_EQ(output, Bytes(output.size(), 0xa5));
  page.functions[1].function_id = JW_CHANNEL_NONE;
  EXPECT_EQ(jw_encode_discovery_page(output.data(), output.size(), &page), -1);
  page.count = 5;
  EXPECT_EQ(jw_encode_discovery_page(output.data(), output.size(), &page), -1);
}
TEST(JoshuaWireCommands, ProfileEnvelopeChecksVersionsAndAdvertisedOperations) {
  const uint8_t data[] = {10, 11};
  jw_profile_request_t req{0xbeef, 7, 8, 31, data, 2};
  uint8_t bytes[7];
  ASSERT_EQ(jw_encode_profile_request(bytes, sizeof(bytes), &req), 7);
  EXPECT_EQ(Bytes(bytes, bytes + 7), (Bytes{0xef, 0xbe, 7, 8, 31, 10, 11}));
  jw_profile_request_t decoded{};
  CheckBounds(
      7,
      [&](uint8_t* p, size_t n) { return jw_encode_profile_request(p, n, &req); },
      [&](const uint8_t* p, size_t n) { return jw_decode_profile_request(p, n, &decoded); },
      JW_PROFILE_HEADER_LEN);
  jw_function_descriptor_t f{1, 1, 0xbeef, 7, 9, 1u << 31, 0};
  EXPECT_TRUE(jw_function_supports_operation(&f, &decoded));
  decoded.major = 6;
  EXPECT_FALSE(jw_function_supports_operation(&f, &decoded));
  decoded.major = 7;
  decoded.minor = 10;
  EXPECT_FALSE(jw_function_supports_operation(&f, &decoded));
  decoded.minor = 9;
  decoded.operation = 1;
  EXPECT_FALSE(jw_function_supports_operation(&f, &decoded));
  decoded.operation = 32;
  EXPECT_FALSE(jw_function_supports_operation(&f, &decoded));
  EXPECT_FALSE(jw_function_supports_operation(nullptr, &decoded));
}
TEST(JoshuaWireCommands, SafetyStateAndDependenciesAreQueryableAndBounded) {
  jw_device_status_t v{JW_STATE_UNAVAILABLE,
                       JW_REASON_SHUTDOWN_DEPENDENCY,
                       JW_RECOVERY_CLEAR_ESTOP,
                       JW_STOP_ACTION_COMPLETE,
                       JW_SAFE_UNSUPPORTED,
                       3,
                       4,
                       5,
                       JW_EXECUTION_COMPLETE,
                       JW_STATUS_OK};
  jw_device_status_t result{};
  CheckBounds(
      JW_DEVICE_STATUS_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_device_status(p, n, &v); },
      [&](const uint8_t* p, size_t n) { return jw_decode_device_status(p, n, &result); });
  EXPECT_EQ(result.reason, JW_REASON_SHUTDOWN_DEPENDENCY);
  EXPECT_EQ(result.verification, JW_SAFE_UNSUPPORTED);
  EXPECT_EQ(result.action_message_id, 5);
  uint8_t state_bytes[JW_DEVICE_STATUS_LEN];
  v.verification = JW_SAFE_VERIFIED;
  EXPECT_EQ(jw_encode_device_status(state_bytes, sizeof(state_bytes), &v), JW_DEVICE_STATUS_LEN);
  v.stop_phase = JW_STOP_ACCEPTED;
  EXPECT_EQ(jw_encode_device_status(state_bytes, sizeof(state_bytes), &v), -1);
  v.verification = JW_SAFE_UNKNOWN;
  v.action_execution = JW_EXECUTION_ACCEPTED;
  EXPECT_EQ(jw_encode_device_status(state_bytes, sizeof(state_bytes), &v), JW_DEVICE_STATUS_LEN);

  jw_dependency_page_t page{};
  page.generation = 1;
  page.next_cursor = JW_CURSOR_END;
  page.count = 16;
  for (uint8_t i = 0; i < page.count; ++i) page.dependencies[i] = {0, i, JW_SHUTDOWN_POWER};
  jw_dependency_page_t decoded{};
  CheckBounds(
      JW_DEPENDENCY_MAX_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_dependency_page(p, n, &page); },
      [&](const uint8_t* p, size_t n) { return jw_decode_dependency_page(p, n, &decoded); });
  EXPECT_EQ(decoded.count, 16);
}
TEST(JoshuaWireProfiles, MeasurementAndSchemaGoldenBytes) {
  jw_sensor_sample_t v{-12.5f, kMetadata}, result{};
  Bytes bytes(JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN);
  ASSERT_EQ(jw_encode_sensor_sample_payload(
                bytes.data(), bytes.size(), JW_SENSOR_INTERFACE_SCALAR_F32, &v),
            bytes.size());
  auto expected = kMetadataBytes;
  expected.insert(expected.end(), {0, 0, 0x48, 0xc1});
  EXPECT_EQ(bytes, expected);
  CheckBounds(
      bytes.size(),
      [&](uint8_t* p, size_t n) {
        return jw_encode_sensor_sample_payload(p, n, JW_SENSOR_INTERFACE_SCALAR_F32, &v);
      },
      [&](const uint8_t* p, size_t n) {
        return jw_decode_sensor_sample_payload(p, n, JW_SENSOR_INTERFACE_SCALAR_F32, &result);
      });
  EXPECT_EQ(result.metadata.timestamp_us, kMetadata.timestamp_us);
  EXPECT_EQ(result.metadata.boot_id, kMetadata.boot_id);
  EXPECT_FLOAT_EQ(result.value, -12.5f);
  jw_sensor_schema_t schema{
      JW_SENSOR_INTERFACE_SCALAR_F32, JW_UNIT_CELSIUS, JW_CLOCK_BOOT_MONOTONIC_US, 10000},
      schema_result{};
  CheckBounds(
      JW_SENSOR_SCHEMA_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_sensor_schema(p, n, &schema); },
      [&](const uint8_t* p, size_t n) { return jw_decode_sensor_schema(p, n, &schema_result); });
  EXPECT_EQ(schema_result.unit, JW_UNIT_CELSIUS);
  v.value = std::numeric_limits<float>::quiet_NaN();
  EXPECT_EQ(jw_encode_sensor_sample_payload(
                bytes.data(), bytes.size(), JW_SENSOR_INTERFACE_SCALAR_F32, &v),
            -1);
}
TEST(JoshuaWireProfiles, ActuatorTargetAndFeedbackStayActuatorSpecific) {
  uint8_t bytes[JW_FEEDBACK_RESPONSE_PAYLOAD_LEN];
  ASSERT_EQ(jw_encode_set_target_payload(bytes, sizeof(bytes), JW_MODE_VELOCITY, -12.5f), 5);
  EXPECT_EQ(Bytes(bytes, bytes + 5), (Bytes{2, 0, 0, 0x48, 0xc1}));
  jw_set_target_t target{};
  CheckBounds(
      JW_SET_TARGET_PAYLOAD_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_set_target_payload(p, n, JW_MODE_POSITION, 1); },
      [&](const uint8_t* p, size_t n) { return jw_decode_set_target_payload(p, n, &target); });
  jw_complex_target_t complex{1, -2, 0.5};
  ASSERT_EQ(jw_encode_complex_target_payload(bytes, sizeof(bytes), &complex), 13);
  EXPECT_EQ(Bytes(bytes, bytes + 13), (Bytes{4, 0, 0, 0x80, 0x3f, 0, 0, 0, 0xc0, 0, 0, 0, 0x3f}));
  ASSERT_EQ(jw_decode_set_target_payload(bytes, 13, &target), JW_RESULT_OK);
  EXPECT_FLOAT_EQ(target.combined.torque, 0.5);
  CheckBounds(
      13,
      [&](uint8_t* p, size_t n) { return jw_encode_complex_target_payload(p, n, &complex); },
      [&](const uint8_t* p, size_t n) { return jw_decode_set_target_payload(p, n, &target); });
  jw_feedback_t feedback{1, -2, 0xabcd, kMetadata}, decoded{};
  ASSERT_EQ(jw_encode_feedback_payload(bytes, sizeof(bytes), &feedback), sizeof(bytes));
  auto expected = kMetadataBytes;
  expected.insert(expected.end(), {0, 0, 0x80, 0x3f, 0, 0, 0, 0xc0, 0xcd, 0xab});
  EXPECT_EQ(Bytes(bytes, bytes + sizeof(bytes)), expected);
  CheckBounds(
      sizeof(bytes),
      [&](uint8_t* p, size_t n) { return jw_encode_feedback_payload(p, n, &feedback); },
      [&](const uint8_t* p, size_t n) { return jw_decode_feedback_payload(p, n, &decoded); });
  EXPECT_FLOAT_EQ(decoded.position, 1);
  jw_actuator_schema_t schema{JW_DRIVE_STEP_DIR,
                              0x1e,
                              JW_UNIT_RADIAN,
                              JW_UNIT_RADIAN_PER_SECOND,
                              JW_UNIT_NATIVE,
                              JW_CLOCK_BOOT_MONOTONIC_US,
                              1000},
      s{};
  CheckBounds(
      JW_ACTUATOR_SCHEMA_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_actuator_schema(p, n, &schema); },
      [&](const uint8_t* p, size_t n) { return jw_decode_actuator_schema(p, n, &s); });
  EXPECT_EQ(s.drive, JW_DRIVE_STEP_DIR);
  bytes[0] = 0;
  EXPECT_EQ(jw_decode_set_target_payload(bytes, 5, &target), JW_RESULT_ERROR);
}
TEST(JoshuaWireProfiles, StepDirConfigurationAndTypedIo) {
  jw_configure_step_dir_t v{0x12345678, 1, 1, 4, 5, 6, 0x2345}, result{};
  uint8_t bytes[32];
  ASSERT_EQ(jw_encode_configure_step_dir_payload(bytes, sizeof(bytes), &v), 11);
  EXPECT_EQ(Bytes(bytes, bytes + 11), (Bytes{0x78, 0x56, 0x34, 0x12, 1, 1, 4, 5, 6, 0x45, 0x23}));
  CheckBounds(
      11,
      [&](uint8_t* p, size_t n) { return jw_encode_configure_step_dir_payload(p, n, &v); },
      [&](const uint8_t* p, size_t n) {
        return jw_decode_configure_step_dir_payload(p, n, &result);
      });
  jw_io_value_t value{};
  value.interface = JW_IO_INTERFACE_DIGITAL;
  value.value.digital = 1;
  jw_io_value_t decoded{};
  CheckBounds(
      1,
      [&](uint8_t* p, size_t n) { return jw_encode_io_payload(p, n, &value); },
      [&](const uint8_t* p, size_t n) {
        return jw_decode_io_payload(p, n, value.interface, &decoded);
      });
  jw_io_sample_t sample{value, kMetadata}, decoded_sample{};
  CheckBounds(
      JW_MEASUREMENT_METADATA_LEN + 1,
      [&](uint8_t* p, size_t n) { return jw_encode_io_sample(p, n, &sample); },
      [&](const uint8_t* p, size_t n) {
        return jw_decode_io_sample(p, n, value.interface, &decoded_sample);
      });
  EXPECT_EQ(decoded_sample.value.value.digital, 1);
  jw_io_schema_t schema{
      JW_IO_INTERFACE_DIGITAL, JW_UNIT_DIMENSIONLESS, JW_CLOCK_BOOT_MONOTONIC_US, 1000},
      schema_result{};
  CheckBounds(
      JW_IO_SCHEMA_LEN,
      [&](uint8_t* p, size_t n) { return jw_encode_io_schema(p, n, &schema); },
      [&](const uint8_t* p, size_t n) { return jw_decode_io_schema(p, n, &schema_result); });
  EXPECT_EQ(schema_result.unit, JW_UNIT_DIMENSIONLESS);
  value.value.digital = 2;
  EXPECT_EQ(jw_encode_io_payload(bytes, sizeof(bytes), &value), -1);
  value.interface = JW_IO_INTERFACE_ANALOG_F32;
  value.value.analog = 1;
  CheckBounds(
      4,
      [&](uint8_t* p, size_t n) { return jw_encode_io_payload(p, n, &value); },
      [&](const uint8_t* p, size_t n) {
        return jw_decode_io_payload(p, n, value.interface, &decoded);
      });
  EXPECT_FLOAT_EQ(decoded.value.analog, 1);
}
