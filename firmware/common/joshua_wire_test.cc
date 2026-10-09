// Hardware-free tests for the shared JW codec, firmware session and
// shared endpoint: golden bytes, CRC/bounds, version separation,
// correlation, reset/reboot and duplicate/stale-request handling.
#include "firmware/common/joshua_wire.h"

#include <array>
#include <cstring>
#include <vector>

#include "firmware/common/joshua_wire_endpoint.h"
#include "firmware/common/joshua_wire_firmware_session.h"
#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;

Bytes Request(
    uint32_t session, uint32_t id, uint8_t cmd, uint8_t channel = 0xFF, const Bytes& payload = {}) {
  Bytes bytes(JW_MAX_FRAME_LEN);
  const int len = jw_encode_frame(bytes.data(),
                                  bytes.size(),
                                  session,
                                  id,
                                  cmd,
                                  channel,
                                  payload.data(),
                                  static_cast<uint8_t>(payload.size()));
  EXPECT_GT(len, 0);
  bytes.resize(len > 0 ? len : 0);
  return bytes;
}

TEST(JoshuaWire, ConfigurePayloadMatchesIndependentGoldenBytes) {
  jw_configure_step_dir_t config{};
  config.max_pulse_rate_hz = 1000;
  config.enable_active_low = 1;
  config.step_pin = 2;
  config.dir_pin = 3;
  config.enable_pin = 4;
  config.step_pulse_width_us = 20;
  uint8_t encoded[JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN];
  const int len = jw_encode_configure_step_dir_payload(encoded, sizeof(encoded), &config);
  ASSERT_EQ(len, sizeof(encoded));
  const Bytes expected{0xe8, 0x03, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x14, 0x00};
  EXPECT_EQ(Bytes(encoded, encoded + len), expected);
  const auto bytes = Request(100, 2, JW_CMD_CONFIGURE_CHANNEL, 0, expected);
  jw_frame_t frame;
  ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
  EXPECT_EQ(frame.cmd, JW_CMD_CONFIGURE_CHANNEL);
  EXPECT_EQ(Bytes(frame.payload, frame.payload + frame.payload_len), expected);
  EXPECT_EQ(frame.proto_ver, 2);
  EXPECT_STREQ(JW_VERSION_STRING, "0.0.2");
  EXPECT_EQ(JW_VERSION_MAJOR, 0);
  EXPECT_EQ(JW_VERSION_MINOR, 0);
  EXPECT_EQ(JW_VERSION_PATCH, 2);
}

TEST(JoshuaWire, ResetAndTargetMatchIndependentGoldenBytes) {
  // CRC fixtures computed independently with Python binascii.crc_hqx(body, 0xffff).
  EXPECT_EQ(Request(0x12345678, 0x01020304, JW_CMD_RESET_SESSION),
            (Bytes{0xa5,
                   0x0b,
                   0x02,
                   0x78,
                   0x56,
                   0x34,
                   0x12,
                   0x04,
                   0x03,
                   0x02,
                   0x01,
                   0x08,
                   0xff,
                   0x82,
                   0x0c}));
  EXPECT_EQ(
      Request(0x12345678, 0x01020305, JW_CMD_SET_TARGET, 2, {JW_MODE_POSITION, 0, 0, 0x48, 0x41}),
      (Bytes{0xa5, 0x10, 0x02, 0x78, 0x56, 0x34, 0x12, 0x05, 0x03, 0x02,
             0x01, 0x03, 0x02, 1,    0,    0,    0x48, 0x41, 0x33, 0x81}));
}

TEST(JoshuaWire, CrcCoversEveryHeaderAndPayloadBit) {
  const auto original = Request(0x12345678, 0x01020305, JW_CMD_SET_TARGET, 2, {1, 2, 3, 4, 5});
  for (size_t i = 0; i < original.size(); ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      auto corrupt = original;
      corrupt[i] ^= 1 << bit;
      jw_frame_t decoded;
      EXPECT_EQ(jw_decode_frame(corrupt.data(), corrupt.size(), &decoded), JW_RESULT_ERROR)
          << i << ":" << bit;
    }
  }
}

TEST(JoshuaWire, BoundsVersionsAndReservedIdsAreRejected) {
  std::array<uint8_t, 65> buffer{};
  Bytes payload(JW_MAX_PAYLOAD_LEN, 0xC3);
  EXPECT_EQ(jw_encode_frame(
                buffer.data(), 64, UINT32_MAX, UINT32_MAX, 3, 0, payload.data(), payload.size()),
            64);
  jw_frame_t decoded;
  ASSERT_EQ(jw_decode_frame(buffer.data(), 64, &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.session_id, UINT32_MAX);
  EXPECT_EQ(decoded.message_id, UINT32_MAX);
  EXPECT_EQ(decoded.payload_len, JW_MAX_PAYLOAD_LEN);
  for (size_t len = 0; len < 64; ++len) {
    EXPECT_EQ(jw_decode_frame(buffer.data(), len, &decoded), JW_RESULT_ERROR);
  }
  EXPECT_EQ(jw_decode_frame(buffer.data(), 65, &decoded), JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_frame(nullptr, 64, &decoded), JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_frame(buffer.data(), 64, nullptr), JW_RESULT_ERROR);
  EXPECT_EQ(jw_encode_frame(buffer.data(), 63, 1, 1, 3, 0, payload.data(), payload.size()), -1);
  EXPECT_EQ(jw_encode_frame(buffer.data(), 65, 1, 1, 3, 0, payload.data(), 50), -1);
  EXPECT_EQ(jw_encode_frame(buffer.data(), 64, 0, 1, 3, 0, nullptr, 0), -1);
  EXPECT_EQ(jw_encode_frame(buffer.data(), 64, 1, 0, 3, 0, nullptr, 0), -1);
  EXPECT_EQ(jw_encode_frame(buffer.data(), 64, 1, 1, 3, 0, nullptr, 1), -1);
  EXPECT_EQ(jw_encode_frame(nullptr, 64, 1, 1, 3, 0, nullptr, 0), -1);
  const Bytes legacy{0xa5, 0x03, 0x01, 0x05, 0x00, 0x59, 0x04};
  EXPECT_EQ(jw_decode_frame(legacy.data(), legacy.size(), &decoded), JW_RESULT_ERROR);
}

TEST(JoshuaWire, PayloadLengthCannotUnderflowOrExceedLimit) {
  auto bytes = Request(1, 1, JW_CMD_RESET_SESSION);
  jw_frame_t decoded{};
  ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.payload_len, 0);

  for (int body_len = 0; body_len <= UINT8_MAX; ++body_len) {
    if (body_len >= JW_HEADER_BODY_LEN && body_len <= JW_HEADER_BODY_LEN + JW_MAX_PAYLOAD_LEN) {
      continue;
    }
    bytes[JW_LENGTH_OFFSET] = static_cast<uint8_t>(body_len);
    decoded.payload_len = 0xa5;
    EXPECT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &decoded), JW_RESULT_ERROR) << body_len;
    EXPECT_EQ(decoded.payload_len, 0xa5) << body_len;
  }
}

TEST(JoshuaWire, ResponseCopiesAndMatchesAllCorrelationFields) {
  const auto bytes = Request(44, 91, JW_CMD_ENABLE, 2);
  jw_frame_t request;
  ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &request), JW_RESULT_OK);
  uint8_t response[JW_MAX_FRAME_LEN];
  const uint8_t ok = JW_STATUS_OK;
  const int len = jw_encode_response(response, sizeof(response), &request, &ok, 1);
  jw_frame_t decoded;
  ASSERT_EQ(jw_decode_frame(response, len, &decoded), JW_RESULT_OK);
  EXPECT_TRUE(jw_response_matches(&request, &decoded));
  EXPECT_EQ(jw_check_response(&request, &decoded), JW_MATCH_OK);
  const jw_match_result_t failures[] = {
      JW_MATCH_SESSION_MISMATCH,
      JW_MATCH_MESSAGE_MISMATCH,
      JW_MATCH_COMMAND_MISMATCH,
      JW_MATCH_CHANNEL_MISMATCH,
      JW_MATCH_INVALID_VERSION,
  };
  for (int field = 0; field < 5; ++field) {
    auto wrong = decoded;
    switch (field) {
      case 0:
        ++wrong.session_id;
        break;
      case 1:
        ++wrong.message_id;
        break;
      case 2:
        ++wrong.cmd;
        break;
      case 3:
        ++wrong.channel;
        break;
      case 4:
        ++wrong.proto_ver;
        break;
    }
    EXPECT_FALSE(jw_response_matches(&request, &wrong));
    EXPECT_EQ(jw_check_response(&request, &wrong), failures[field]);
  }
  EXPECT_FALSE(jw_response_matches(nullptr, &decoded));
  EXPECT_FALSE(jw_response_matches(&request, nullptr));
  EXPECT_EQ(jw_check_response(nullptr, &decoded), JW_MATCH_NULL_ARGUMENT);
  EXPECT_EQ(jw_check_response(&request, nullptr), JW_MATCH_NULL_ARGUMENT);
  EXPECT_EQ(jw_encode_response(nullptr, sizeof(response), &request, &ok, 1), -1);
  EXPECT_EQ(jw_encode_response(response, sizeof(response), nullptr, &ok, 1), -1);
  EXPECT_EQ(jw_encode_response(response, sizeof(response), &request, nullptr, 1), -1);
  EXPECT_GT(jw_encode_response(response, sizeof(response), &request, nullptr, 0), 0);
}

TEST(JoshuaWire, ResponseDiagnosticsRejectInvalidIdsAndReportFirstFailure) {
  jw_frame_t valid{};
  valid.proto_ver = JW_PROTO_VERSION;
  valid.session_id = 1;
  valid.message_id = 2;
  valid.cmd = JW_CMD_ENABLE;

  auto invalid = valid;
  invalid.session_id = 0;
  EXPECT_EQ(jw_check_response(&invalid, &valid), JW_MATCH_INVALID_SESSION_ID);
  EXPECT_EQ(jw_check_response(&valid, &invalid), JW_MATCH_INVALID_SESSION_ID);
  EXPECT_FALSE(jw_response_matches(&invalid, &invalid));

  invalid = valid;
  invalid.message_id = 0;
  EXPECT_EQ(jw_check_response(&invalid, &valid), JW_MATCH_INVALID_MESSAGE_ID);
  EXPECT_EQ(jw_check_response(&valid, &invalid), JW_MATCH_INVALID_MESSAGE_ID);
  EXPECT_FALSE(jw_response_matches(&invalid, &invalid));

  // Version validation precedes invalid IDs and correlation mismatches.
  invalid.proto_ver = 0;
  invalid.session_id = 3;
  invalid.cmd = JW_CMD_DISABLE;
  EXPECT_EQ(jw_check_response(&invalid, &valid), JW_MATCH_INVALID_VERSION);
  EXPECT_EQ(jw_check_response(&valid, &invalid), JW_MATCH_INVALID_VERSION);
  EXPECT_FALSE(jw_response_matches(&invalid, &invalid));
  EXPECT_EQ(jw_check_response(nullptr, &invalid), JW_MATCH_NULL_ARGUMENT);
}

class FirmwareSessionTest : public ::testing::Test {
 protected:
  struct Device {
    int calls = 0;
    int resets = 0;
    bool enabled = false;
    bool fail = false;
  } device;
  jw_firmware_session_t session{};
  Bytes response;

  static int Handle(void* context, const jw_frame_t* request, uint8_t* payload, size_t) {
    auto& state = *static_cast<Device*>(context);
    ++state.calls;
    if (request->cmd == JW_CMD_ENABLE) state.enabled = true;
    if (state.fail) return -1;
    payload[0] = JW_STATUS_OK;
    return 1;
  }
  static void Reset(void* context) {
    auto& state = *static_cast<Device*>(context);
    ++state.resets;
    state.enabled = false;
  }
  int Process(const Bytes& request) {
    response.resize(JW_MAX_FRAME_LEN);
    const int len = jw_firmware_session_process(&session,
                                                request.data(),
                                                request.size(),
                                                response.data(),
                                                response.size(),
                                                Handle,
                                                Reset,
                                                &device);
    response.resize(len > 0 ? len : 0);
    return len;
  }
};

TEST_F(FirmwareSessionTest, ResetRequiredAndGoldenResponse) {
  EXPECT_EQ(Process(Request(0x12345678, 1, JW_CMD_CONFIGURE_CHANNEL, 0)), 0);
  EXPECT_EQ(device.calls, 0);
  EXPECT_GT(Process(Request(0x12345678, 0x01020304, JW_CMD_RESET_SESSION)), 0);
  EXPECT_EQ(response,
            (Bytes{0xa5,
                   0x0c,
                   0x02,
                   0x78,
                   0x56,
                   0x34,
                   0x12,
                   0x04,
                   0x03,
                   0x02,
                   0x01,
                   0x08,
                   0xff,
                   0x01,
                   0xad,
                   0x53}));
  EXPECT_EQ(device.resets, 1);
}

TEST_F(FirmwareSessionTest, DuplicateReplayOldIdsAndChangedRequestAreSafe) {
  const auto reset = Request(7, 1, JW_CMD_RESET_SESSION);
  ASSERT_GT(Process(reset), 0);
  auto reset_response = response;
  EXPECT_GT(Process(reset), 0);
  EXPECT_EQ(response, reset_response);
  EXPECT_EQ(device.resets, 1);
  const auto enable = Request(7, 2, JW_CMD_ENABLE, 0);
  ASSERT_GT(Process(enable), 0);
  const auto saved = response;
  ASSERT_GT(Process(enable), 0);
  EXPECT_EQ(response, saved);
  EXPECT_EQ(device.calls, 1);
  EXPECT_EQ(Process(Request(7, 2, JW_CMD_DISABLE, 0)), 0);
  EXPECT_EQ(Process(Request(7, 2, JW_CMD_ENABLE, 1)), 0);
  EXPECT_EQ(Process(Request(7, 2, JW_CMD_ENABLE, 0, {9})), 0);
  EXPECT_EQ(Process(reset), 0);
  EXPECT_TRUE(device.enabled);
  EXPECT_GT(Process(Request(7, 3, JW_CMD_GET_FEEDBACK, 0)), 0);
  EXPECT_EQ(Process(enable), 0);
  EXPECT_EQ(device.calls, 2);
}

TEST_F(FirmwareSessionTest, ReconnectDisablesAndRebootRequiresHandshakeAgain) {
  ASSERT_GT(Process(Request(7, 1, JW_CMD_RESET_SESSION)), 0);
  ASSERT_GT(Process(Request(7, 2, JW_CMD_ENABLE, 0)), 0);
  ASSERT_TRUE(device.enabled);
  ASSERT_GT(Process(Request(8, 1, JW_CMD_RESET_SESSION)), 0);
  EXPECT_FALSE(device.enabled);
  EXPECT_EQ(Process(Request(7, 3, JW_CMD_ENABLE, 0)), 0);
  EXPECT_GT(Process(Request(8, 2, JW_CMD_ENABLE, 0)), 0);
  jw_firmware_session_init(&session);  // Reboot clears session and retained response.
  EXPECT_EQ(Process(Request(8, 3, JW_CMD_ENABLE, 0)), 0);
  EXPECT_EQ(session.response_len, 0);
}

TEST_F(FirmwareSessionTest, InvalidResetCannotChangeSession) {
  EXPECT_EQ(Process(Request(5, 1, JW_CMD_RESET_SESSION, 0)), 0);
  EXPECT_EQ(Process(Request(5, 1, JW_CMD_RESET_SESSION, 0xff, {0})), 0);
  EXPECT_EQ(device.resets, 0);
  ASSERT_GT(Process(Request(5, 1, JW_CMD_RESET_SESSION)), 0);
  EXPECT_EQ(Process(Request(5, 100, JW_CMD_RESET_SESSION)), 0);
  EXPECT_EQ(session.last_message_id, 1);
}

TEST_F(FirmwareSessionTest, HandlerFailureConsumesIdAndWrapIsRejected) {
  ASSERT_GT(Process(Request(5, 1, JW_CMD_RESET_SESSION)), 0);
  device.fail = true;
  const auto command = Request(5, UINT32_MAX, JW_CMD_ENABLE, 0);
  EXPECT_EQ(Process(command), -1);
  device.fail = false;
  EXPECT_EQ(Process(command), 0);
  EXPECT_EQ(Process(Request(5, 2, JW_CMD_ENABLE, 0)), 0);
  EXPECT_EQ(device.calls, 1);
  ASSERT_GT(Process(Request(6, 1, JW_CMD_RESET_SESSION)), 0);
  EXPECT_GT(Process(Request(6, 2, JW_CMD_ENABLE, 0)), 0);
}

int NeutralHandler(void*, const jw_command_t*, uint8_t* out, size_t cap) {
  return jw_encode_status_payload(JW_STATUS_OK, out, cap);
}
void NoopReset(void*) {}

TEST(JoshuaWireEndpoint, RejectsLegacyFramesAndRequiresReset) {
  jw_endpoint_t endpoint;
  jw_endpoint_init(&endpoint);
  uint8_t response[JW_MAX_FRAME_LEN];
  const Bytes legacy{0xa5, 0x03, 0x01, 0x05, 0x00, 0x59, 0x04};
  auto process = [&](const Bytes& request) {
    return jw_endpoint_process(&endpoint,
                               request.data(),
                               request.size(),
                               response,
                               sizeof(response),
                               NeutralHandler,
                               NoopReset,
                               nullptr);
  };
  EXPECT_EQ(process(legacy), 0);
  EXPECT_EQ(process(Request(1, 1, JW_CMD_ENABLE, 0)), 0);
  EXPECT_GT(process(Request(1, 1, JW_CMD_RESET_SESSION)), 0);
  const auto request = Request(1, 2, JW_CMD_ENABLE, 0);
  const int length = process(request);
  ASSERT_GT(length, 0);
  jw_frame_t sent, reply;
  ASSERT_EQ(jw_decode_frame(request.data(), request.size(), &sent), JW_RESULT_OK);
  ASSERT_EQ(jw_decode_frame(response, length, &reply), JW_RESULT_OK);
  EXPECT_TRUE(jw_response_matches(&sent, &reply));
  EXPECT_EQ(reply.payload[0], JW_STATUS_OK);
  EXPECT_EQ(process(legacy), 0);
}
}  // namespace
