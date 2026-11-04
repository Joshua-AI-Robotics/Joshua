#include "firmware/common/joshua_wire_sensor_stream.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <vector>

#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;
const jw_stream_sample_t kSample{
    0x01020304,
    0x05060708,
    7,
    3,
    {1.5f, {JW_SAMPLE_VALID, JW_CLOCK_BOOT_MONOTONIC_US, 0x11223344, 0x0102030405060708ULL, 9}}};
// Independent little-endian layout and CRC-16/CCITT-FALSE golden frame.
const Bytes kGolden{0xa5, 0x2a, 0x04, 0x04, 0x03, 0x02, 0x01, 0x08, 0x07, 0x06, 0x05, 0x11,
                    0x07, 0x02, 0x00, 0x01, 0x01, 0x01, 0x03, 0x00, 0x00, 0x00, 0x01, 0x01,
                    0x44, 0x33, 0x22, 0x11, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
                    0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0xc0, 0x3f, 0x0a, 0x28};

TEST(SensorStreamCodec, NotificationHasGoldenLayoutAndPreservesSampleIdentity) {
  EXPECT_EQ(JW_PROTO_VERSION, 4);
  EXPECT_EQ(JW_PROFILE_VERSION_MINOR, 0);  // Polling/actuator/I/O versions stay stable.
  EXPECT_EQ(JW_STREAM_SAMPLE_FRAME_LEN, 46);
  Bytes encoded(JW_STREAM_SAMPLE_FRAME_LEN);
  ASSERT_EQ(jw_encode_stream_sample(encoded.data(), encoded.size(), &kSample), encoded.size());
  EXPECT_EQ(encoded, kGolden);
  jw_stream_sample_t decoded{};
  ASSERT_EQ(jw_decode_stream_sample(kGolden.data(), kGolden.size(), &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.session_id, kSample.session_id);
  EXPECT_EQ(decoded.subscription_id, kSample.subscription_id);
  EXPECT_EQ(decoded.function_id, 7);
  EXPECT_EQ(decoded.dropped_acquisitions, 3u);
  EXPECT_EQ(decoded.sample.value, 1.5f);
  EXPECT_EQ(decoded.sample.metadata.boot_id, 0x11223344u);
  EXPECT_EQ(decoded.sample.metadata.timestamp_us, 0x0102030405060708ULL);
  EXPECT_EQ(decoded.sample.metadata.sequence, 9u);
}

TEST(SensorStreamCodec, BoundsAndInvalidInputsDoNotPublishPartialOutputs) {
  Bytes bytes(JW_STREAM_SAMPLE_FRAME_LEN + 1, 0xcc);
  EXPECT_EQ(jw_encode_stream_sample(nullptr, bytes.size(), &kSample), -1);
  EXPECT_EQ(jw_encode_stream_sample(bytes.data(), bytes.size(), nullptr), -1);
  for (size_t cap = 0; cap < JW_STREAM_SAMPLE_FRAME_LEN; ++cap) {
    EXPECT_EQ(jw_encode_stream_sample(bytes.data(), cap, &kSample), -1);
    EXPECT_EQ(bytes, Bytes(bytes.size(), 0xcc));
  }
  for (int field = 0; field < 4; ++field) {
    auto invalid = kSample;
    if (field == 0) invalid.session_id = 0;
    if (field == 1) invalid.subscription_id = 0;
    if (field == 2) invalid.function_id = JW_CHANNEL_NONE;
    if (field == 3) invalid.sample.value = std::numeric_limits<float>::infinity();
    EXPECT_EQ(jw_encode_stream_sample(bytes.data(), bytes.size(), &invalid), -1);
    EXPECT_EQ(bytes, Bytes(bytes.size(), 0xcc));
  }
  ASSERT_EQ(jw_encode_stream_sample(bytes.data(), bytes.size(), &kSample), kGolden.size());
  EXPECT_EQ(bytes.back(), 0xcc);
  for (size_t len = 0; len <= bytes.size(); ++len) {
    jw_stream_sample_t out{};
    out.session_id = 99;
    const auto status = jw_decode_stream_sample(bytes.data(), len, &out);
    EXPECT_EQ(status, len == kGolden.size() ? JW_RESULT_OK : JW_RESULT_ERROR);
    if (status != JW_RESULT_OK) {
      EXPECT_EQ(out.session_id, 99u);
    }
  }
  jw_stream_sample_t out{};
  EXPECT_EQ(jw_decode_stream_sample(nullptr, bytes.size(), &out), JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_stream_sample(bytes.data(), kGolden.size(), nullptr), JW_RESULT_ERROR);
}

TEST(SensorStreamCodec, RejectsMalformedTypedPayloadEvenWithValidFrameCrc) {
  jw_frame_t frame{};
  ASSERT_EQ(jw_decode_frame(kGolden.data(), kGolden.size(), &frame), JW_RESULT_OK);
  // Profile, major, minor, type, validity, clock, and boot must be usable.
  for (size_t offset : {0u, 1u, 2u, 3u, 4u, 9u, 10u, 11u}) {
    Bytes payload(frame.payload, frame.payload + frame.payload_len);
    payload[offset] = 0xff;
    if (offset == 11) {  // Zero boot ID is invalid; arbitrary nonzero IDs are legal.
      for (size_t i = 11; i < 15; ++i) payload[i] = 0;
    }
    Bytes altered(JW_MAX_FRAME_LEN);
    const int len = jw_encode_frame(altered.data(),
                                    altered.size(),
                                    frame.session_id,
                                    frame.message_id,
                                    frame.cmd,
                                    frame.channel,
                                    payload.data(),
                                    payload.size());
    ASSERT_GT(len, 0);
    jw_stream_sample_t out{};
    EXPECT_EQ(jw_decode_stream_sample(altered.data(), len, &out), JW_RESULT_ERROR);
  }
  Bytes corrupt = kGolden;
  corrupt.back() ^= 1;
  jw_stream_sample_t out{};
  EXPECT_EQ(jw_decode_stream_sample(corrupt.data(), corrupt.size(), &out), JW_RESULT_ERROR);
}

TEST(SensorStreamCodec, InvalidMeasurementsRemainExplicitAndCannotMatchReplies) {
  auto invalid = kSample;
  invalid.sample.metadata.validity = JW_SAMPLE_INVALID;
  invalid.sample.value = std::numeric_limits<float>::quiet_NaN();
  invalid.dropped_acquisitions = UINT32_MAX;
  Bytes bytes(JW_STREAM_SAMPLE_FRAME_LEN);
  ASSERT_EQ(jw_encode_stream_sample(bytes.data(), bytes.size(), &invalid), bytes.size());
  jw_stream_sample_t decoded{};
  ASSERT_EQ(jw_decode_stream_sample(bytes.data(), bytes.size(), &decoded), JW_RESULT_OK);
  EXPECT_TRUE(std::isnan(decoded.sample.value));
  EXPECT_EQ(decoded.sample.metadata.validity, JW_SAMPLE_INVALID);
  EXPECT_EQ(decoded.dropped_acquisitions, UINT32_MAX);
  jw_frame_t event{};
  ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &event), JW_RESULT_OK);
  auto request = event;
  request.cmd = JW_CMD_PROFILE;  // All numeric IDs deliberately collide.
  EXPECT_FALSE(jw_response_matches(&request, &event));
  EXPECT_EQ(jw_check_response(&event, &event), JW_MATCH_UNSOLICITED);
  EXPECT_FALSE(jw_response_matches(&event, &event));
}

TEST(SensorStreamCodec, NegotiationRequiresAllCapabilitiesAndRetainsPollingCompatibility) {
  jw_function_descriptor_t function{7, 1, JW_PROFILE_SENSOR, 1, 1, JW_SENSOR_STREAM_OPERATIONS, 0};
  EXPECT_TRUE(jw_sensor_supports_stream(&function));
  jw_profile_request_t read{JW_PROFILE_SENSOR, 1, 0, JW_SENSOR_READ, nullptr, 0};
  EXPECT_TRUE(jw_function_supports_operation(&function, &read));
  jw_profile_request_t subscribe{JW_PROFILE_SENSOR, 1, 1, JW_SENSOR_SUBSCRIBE, nullptr, 0};
  function.profile_minor = 0;
  EXPECT_FALSE(jw_function_supports_operation(&function, &subscribe));
  EXPECT_FALSE(jw_sensor_supports_stream(&function));
  function.profile_minor = 1;
  for (uint8_t bit = 0; bit <= JW_SENSOR_GET_SUBSCRIPTION; ++bit) {
    auto missing = function;
    missing.operations &= ~(1u << bit);
    EXPECT_FALSE(jw_sensor_supports_stream(&missing));
  }
  function.profile_major = 2;
  EXPECT_FALSE(jw_sensor_supports_stream(&function));
  function.profile_major = 1;
  function.profile_id = JW_PROFILE_ACTUATOR;
  EXPECT_FALSE(jw_sensor_supports_stream(&function));
  EXPECT_FALSE(jw_sensor_supports_stream(nullptr));
}

TEST(SensorStreamCodec, ControlPayloadsAreBoundedAndHaveGoldenLayouts) {
  uint8_t bytes[JW_STREAM_STATE_LEN + 1];
  for (auto encode : {jw_encode_stream_period, jw_encode_stream_token}) {
    std::fill(std::begin(bytes), std::end(bytes), 0xcc);
    EXPECT_EQ(encode(bytes, sizeof(bytes), 0), -1);
    for (size_t cap = 0; cap < 4; ++cap) EXPECT_EQ(encode(bytes, cap, 10000), -1);
    EXPECT_EQ(Bytes(bytes, bytes + sizeof(bytes)), Bytes(sizeof(bytes), 0xcc));
    ASSERT_EQ(encode(bytes, sizeof(bytes), 10000), 4);
    EXPECT_EQ(Bytes(bytes, bytes + 4), (Bytes{0x10, 0x27, 0, 0}));
    EXPECT_EQ(encode(nullptr, sizeof(bytes), 1), -1);
  }
  for (auto decode : {jw_decode_stream_period, jw_decode_stream_token}) {
    uint32_t out = 99;
    for (size_t len = 0; len <= sizeof(bytes); ++len) {
      EXPECT_EQ(decode(bytes, len, &out), len == 4 ? JW_RESULT_OK : JW_RESULT_ERROR);
    }
    EXPECT_EQ(decode(nullptr, 4, &out), JW_RESULT_ERROR);
    EXPECT_EQ(decode(bytes, 4, nullptr), JW_RESULT_ERROR);
    const uint8_t zero[4]{};
    EXPECT_EQ(decode(zero, 4, &out), JW_RESULT_ERROR);
  }
  const jw_stream_state_t state{1, 0x05060708, 10000};
  ASSERT_EQ(jw_encode_stream_state(bytes, sizeof(bytes), &state), JW_STREAM_STATE_LEN);
  EXPECT_EQ(Bytes(bytes, bytes + JW_STREAM_STATE_LEN), (Bytes{1, 8, 7, 6, 5, 0x10, 0x27, 0, 0}));
  jw_stream_state_t out{};
  ASSERT_EQ(jw_decode_stream_state(bytes, JW_STREAM_STATE_LEN, &out), JW_RESULT_OK);
  EXPECT_EQ(out.subscription_id, state.subscription_id);
  for (size_t len = 0; len <= sizeof(bytes); ++len)
    EXPECT_EQ(jw_decode_stream_state(bytes, len, &out),
              len == JW_STREAM_STATE_LEN ? JW_RESULT_OK : JW_RESULT_ERROR);
  for (const auto& bad :
       {jw_stream_state_t{2, 1, 10000}, jw_stream_state_t{1, 0, 10000}, jw_stream_state_t{0, 1, 0}})
    EXPECT_EQ(jw_encode_stream_state(bytes, sizeof(bytes), &bad), -1);
}
}  // namespace
