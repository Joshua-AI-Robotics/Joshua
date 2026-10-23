// Hardware-free tests for the shared JW codec, firmware session and
// shared endpoint: golden bytes, CRC/bounds, version separation,
// correlation, reset/reboot and duplicate/stale-request handling.
#include "firmware/common/joshua_wire.h"

#include <array>
#include <cstring>
#include <vector>

#include "firmware/common/joshua_wire_endpoint.h"
#include "firmware/common/joshua_wire_ethercat.h"
#include "firmware/common/joshua_wire_firmware_session.h"
#include "firmware/common/joshua_wire_profiles.h"
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

TEST(JoshuaWire, ResetAndProfileMatchIndependentGoldenBytes) {
  // CRC independently computed using Python binascii.crc_hqx(body, 0xffff).
  EXPECT_EQ(
      Request(0x12345678, 0x01020304, JW_CMD_RESET_SESSION),
      (Bytes{0xa5, 0xb, 0x4, 0x78, 0x56, 0x34, 0x12, 0x4, 0x3, 0x2, 0x1, 0x8, 0xff, 0x76, 0xfd}));
  EXPECT_EQ(Request(0x12345678,
                    0x01020305,
                    JW_CMD_PROFILE,
                    2,
                    {1, 0, 1, 0, JW_ACTUATOR_SET_TARGET, JW_MODE_POSITION, 0, 0, 0x48, 0x41}),
            (Bytes{0xa5, 0x15, 0x4, 0x78, 0x56, 0x34, 0x12, 0x5, 0x3,  0x2,  0x1,  0xb, 0x2,
                   0x1,  0x0,  0x1, 0x0,  0x1,  0x1,  0x0,  0x0, 0x48, 0x41, 0x39, 0xed}));
  EXPECT_STREQ(JW_VERSION_STRING, "0.2.0");
  EXPECT_EQ(JW_PROTO_VERSION, 4);
}

TEST(JoshuaWire, CrcCoversEveryHeaderAndPayloadBit) {
  const auto original = Request(0x12345678, 0x01020305, JW_CMD_PROFILE, 2, {1, 2, 3, 4, 5});
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
  std::array<uint8_t, JW_MAX_FRAME_LEN + 1> buffer{};
  Bytes payload(JW_MAX_PAYLOAD_LEN, 0xC3);
  EXPECT_EQ(jw_encode_frame(buffer.data(),
                            JW_MAX_FRAME_LEN,
                            UINT32_MAX,
                            UINT32_MAX,
                            3,
                            0,
                            payload.data(),
                            payload.size()),
            JW_MAX_FRAME_LEN);
  jw_frame_t decoded;
  ASSERT_EQ(jw_decode_frame(buffer.data(), JW_MAX_FRAME_LEN, &decoded), JW_RESULT_OK);
  EXPECT_EQ(decoded.session_id, UINT32_MAX);
  EXPECT_EQ(decoded.message_id, UINT32_MAX);
  EXPECT_EQ(decoded.payload_len, JW_MAX_PAYLOAD_LEN);
  for (size_t len = 0; len < JW_MAX_FRAME_LEN; ++len) {
    EXPECT_EQ(jw_decode_frame(buffer.data(), len, &decoded), JW_RESULT_ERROR);
  }
  EXPECT_EQ(jw_decode_frame(buffer.data(), JW_MAX_FRAME_LEN + 1, &decoded), JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_frame(nullptr, JW_MAX_FRAME_LEN, &decoded), JW_RESULT_ERROR);
  EXPECT_EQ(jw_decode_frame(buffer.data(), JW_MAX_FRAME_LEN, nullptr), JW_RESULT_ERROR);
  EXPECT_EQ(jw_encode_frame(
                buffer.data(), JW_MAX_FRAME_LEN - 1, 1, 1, 3, 0, payload.data(), payload.size()),
            -1);
  EXPECT_EQ(
      jw_encode_frame(
          buffer.data(), JW_MAX_FRAME_LEN + 1, 1, 1, 3, 0, payload.data(), JW_MAX_PAYLOAD_LEN + 1),
      -1);
  EXPECT_EQ(jw_encode_frame(buffer.data(), JW_MAX_FRAME_LEN, 0, 1, 3, 0, nullptr, 0), -1);
  EXPECT_EQ(jw_encode_frame(buffer.data(), JW_MAX_FRAME_LEN, 1, 0, 3, 0, nullptr, 0), -1);
  EXPECT_EQ(jw_encode_frame(buffer.data(), JW_MAX_FRAME_LEN, 1, 1, 3, 0, nullptr, 1), -1);
  EXPECT_EQ(jw_encode_frame(nullptr, JW_MAX_FRAME_LEN, 1, 1, 3, 0, nullptr, 0), -1);
  const Bytes legacy{0xa5, 0x03, 0x01, 0x05, 0x00, 0x59, 0x04};
  EXPECT_EQ(jw_decode_frame(legacy.data(), legacy.size(), &decoded), JW_RESULT_ERROR);
  const Bytes revision2{
      0xa5, 0x0b, 0x02, 0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01, 0x08, 0xff, 0x82, 0x0c};
  EXPECT_EQ(jw_decode_frame(revision2.data(), revision2.size(), &decoded), JW_RESULT_ERROR);
  const Bytes revision3{0xa5, 0x0b, 3, 0x78, 0x56, 0x34, 0x12, 4, 3, 2, 1, 8, 0xff, 0xcb, 0xd4};
  EXPECT_EQ(jw_decode_frame(revision3.data(), revision3.size(), &decoded), JW_RESULT_ERROR);
}

TEST(JoshuaWire, EthercatLayoutAccommodatesTheFullIdentifyResponse) {
  EXPECT_EQ(JW_MAX_FRAME_LEN, 96);
  EXPECT_LE(JW_DISCOVERY_MAX_LEN + JW_REPLY_HEADER_LEN + JW_FRAME_OVERHEAD, JW_MAX_FRAME_LEN);
  EXPECT_EQ(JWEC_LAYOUT_VERSION, 2);
  EXPECT_EQ(JWEC_DESCRIPTOR_VERSION, 1);
  EXPECT_EQ(JWEC_MAILBOX_SIZE, 108);
  EXPECT_EQ(JWEC_PDO_SIZE, 112);
  EXPECT_EQ(JWEC_MAILBOX_SIZE - JWEC_MAILBOX_FRAME_OFFSET, JW_MAX_FRAME_LEN);
  EXPECT_EQ(JWEC_PDO_SIZE - JWEC_PDO_FRAME_OFFSET, JW_MAX_FRAME_LEN);
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
  const auto bytes = Request(44, 91, JW_CMD_PROFILE, 2);
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
  valid.cmd = JW_CMD_PROFILE;

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
  invalid.cmd = JW_CMD_ESTOP;
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
    if (request->cmd == JW_CMD_PROFILE) state.enabled = true;
    if (state.fail) return -1;
    return jw_encode_reply_payload(
        payload, JW_MAX_PAYLOAD_LEN, JW_STATUS_OK, JW_EXECUTION_COMPLETE, nullptr, 0);
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
  EXPECT_EQ(Process(Request(0x12345678, 1, JW_CMD_PROFILE, 0)), 0);
  EXPECT_EQ(device.calls, 0);
  EXPECT_GT(Process(Request(0x12345678, 0x01020304, JW_CMD_RESET_SESSION)), 0);
  EXPECT_EQ(response,
            (Bytes{0xa5,
                   0xd,
                   0x4,
                   0x78,
                   0x56,
                   0x34,
                   0x12,
                   0x4,
                   0x3,
                   0x2,
                   0x1,
                   0x8,
                   0xff,
                   0x1,
                   0x2,
                   0xbf,
                   0x68}));
  EXPECT_EQ(device.resets, 1);
}

TEST_F(FirmwareSessionTest, DuplicateReplayOldIdsAndChangedRequestAreSafe) {
  const auto reset = Request(7, 1, JW_CMD_RESET_SESSION);
  ASSERT_GT(Process(reset), 0);
  auto reset_response = response;
  EXPECT_GT(Process(reset), 0);
  EXPECT_EQ(response, reset_response);
  EXPECT_EQ(device.resets, 1);
  const auto enable = Request(7, 2, JW_CMD_PROFILE, 0);
  ASSERT_GT(Process(enable), 0);
  const auto saved = response;
  ASSERT_GT(Process(enable), 0);
  EXPECT_EQ(response, saved);
  EXPECT_EQ(device.calls, 1);
  EXPECT_EQ(Process(Request(7, 2, JW_CMD_ESTOP, 0)), 0);
  EXPECT_EQ(Process(Request(7, 2, JW_CMD_PROFILE, 1)), 0);
  EXPECT_EQ(Process(Request(7, 2, JW_CMD_PROFILE, 0, {9})), 0);
  EXPECT_EQ(Process(reset), 0);
  EXPECT_TRUE(device.enabled);
  EXPECT_GT(Process(Request(7, 3, JW_CMD_GET_STATUS, 0)), 0);
  EXPECT_EQ(Process(enable), 0);
  EXPECT_EQ(device.calls, 2);
}

TEST_F(FirmwareSessionTest, ReconnectDisablesAndRebootRequiresHandshakeAgain) {
  ASSERT_GT(Process(Request(7, 1, JW_CMD_RESET_SESSION)), 0);
  ASSERT_GT(Process(Request(7, 2, JW_CMD_PROFILE, 0)), 0);
  ASSERT_TRUE(device.enabled);
  ASSERT_GT(Process(Request(8, 1, JW_CMD_RESET_SESSION)), 0);
  EXPECT_FALSE(device.enabled);
  EXPECT_EQ(Process(Request(7, 3, JW_CMD_PROFILE, 0)), 0);
  EXPECT_GT(Process(Request(8, 2, JW_CMD_PROFILE, 0)), 0);
  jw_firmware_session_init(&session);  // Reboot clears session and retained response.
  EXPECT_EQ(Process(Request(8, 3, JW_CMD_PROFILE, 0)), 0);
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
  const auto command = Request(5, UINT32_MAX, JW_CMD_PROFILE, 0);
  ASSERT_GT(Process(command), 0);
  jw_frame_t frame{};
  ASSERT_EQ(jw_decode_frame(response.data(), response.size(), &frame), JW_RESULT_OK);
  jw_reply_t reply{};
  ASSERT_EQ(jw_decode_reply_payload(frame.payload, frame.payload_len, &reply), JW_RESULT_OK);
  EXPECT_EQ(reply.status, JW_STATUS_OUTCOME_UNKNOWN);
  const auto saved = response;
  device.fail = false;
  EXPECT_GT(Process(command), 0);
  EXPECT_EQ(response, saved);
  EXPECT_EQ(Process(Request(5, 2, JW_CMD_PROFILE, 0)), 0);
  EXPECT_EQ(device.calls, 1);
  ASSERT_GT(Process(Request(6, 1, JW_CMD_RESET_SESSION)), 0);
  EXPECT_GT(Process(Request(6, 2, JW_CMD_PROFILE, 0)), 0);
}

int NeutralHandler(void*, const jw_command_t*, uint8_t* out, size_t cap) {
  return jw_encode_reply_payload(out, cap, JW_STATUS_OK, JW_EXECUTION_COMPLETE, nullptr, 0);
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
  EXPECT_EQ(process(Request(1, 1, JW_CMD_PROFILE, 0)), 0);
  EXPECT_GT(process(Request(1, 1, JW_CMD_RESET_SESSION)), 0);
  const auto request = Request(1, 2, JW_CMD_PROFILE, 0);
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
