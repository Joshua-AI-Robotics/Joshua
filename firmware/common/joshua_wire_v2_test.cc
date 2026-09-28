// Hardware-free tests for the shared v2 codec, firmware session and
// serial endpoint: golden bytes, CRC/bounds, version separation,
// correlation, reset/reboot and duplicate/stale-request handling.
#include "firmware/common/joshua_wire_v2.h"

#include <array>
#include <cstring>
#include <vector>

#include "firmware/common/joshua_wire_serial_endpoint.h"
#include "firmware/common/joshua_wire_v1.h"
#include "firmware/common/joshua_wire_v2_firmware_session.h"
#include "gtest/gtest.h"

namespace {
using Bytes = std::vector<uint8_t>;

Bytes Request(
    uint32_t session, uint32_t id, uint8_t cmd, uint8_t channel = 0xFF, const Bytes& payload = {}) {
  Bytes bytes(JW2_MAX_FRAME_LEN);
  const int len = jw2_encode_frame(bytes.data(),
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

TEST(JoshuaWireV2, ConfigureCommandAndPayloadAreSharedWithV1) {
  jw_configure_step_dir_t config{};
  config.max_pulse_rate_hz = 1000;
  config.enable_active_low = 1;
  config.step_pin = 2;
  config.dir_pin = 3;
  config.enable_pin = 4;
  config.step_pulse_width_us = 20;
  uint8_t encoded[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_configure_channel_step_dir(encoded, sizeof(encoded), 0, &config);
  ASSERT_GT(len, 0);
  jw1_frame_t v1;
  ASSERT_EQ(jw1_decode_frame(encoded, len, &v1), 0);
  const Bytes expected{0xe8, 0x03, 0x00, 0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x14, 0x00};
  EXPECT_EQ(v1.cmd, JW_CMD_CONFIGURE_CHANNEL);
  EXPECT_EQ(Bytes(v1.payload, v1.payload + v1.payload_len), expected);
  const auto bytes = Request(100, 2, JW_CMD_CONFIGURE_CHANNEL, 0, expected);
  jw2_frame_t v2;
  ASSERT_EQ(jw2_decode_frame(bytes.data(), bytes.size(), &v2), 0);
  EXPECT_EQ(v2.cmd, v1.cmd);
  EXPECT_EQ(Bytes(v2.payload, v2.payload + v2.payload_len), expected);
  EXPECT_EQ(v1.proto_ver, 1);
  EXPECT_EQ(v2.proto_ver, 2);
}

TEST(JoshuaWireV2, ResetAndTargetMatchIndependentGoldenBytes) {
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
  EXPECT_EQ(Request(0x12345678, 0x01020305, JW_CMD_SET_TARGET, 2, {0, 0, 0, 0x48, 0x41}),
            (Bytes{0xa5, 0x10, 0x02, 0x78, 0x56, 0x34, 0x12, 0x05, 0x03, 0x02,
                   0x01, 0x03, 0x02, 0,    0,    0,    0x48, 0x41, 0x62, 0x2b}));
}

TEST(JoshuaWireV2, CrcCoversEveryHeaderAndPayloadBit) {
  const auto original = Request(0x12345678, 0x01020305, JW_CMD_SET_TARGET, 2, {1, 2, 3, 4, 5});
  for (size_t i = 0; i < original.size(); ++i) {
    for (int bit = 0; bit < 8; ++bit) {
      auto corrupt = original;
      corrupt[i] ^= 1 << bit;
      jw2_frame_t decoded;
      EXPECT_EQ(jw2_decode_frame(corrupt.data(), corrupt.size(), &decoded), -1) << i << ":" << bit;
    }
  }
}

TEST(JoshuaWireV2, BoundsVersionsAndReservedIdsAreRejected) {
  std::array<uint8_t, 65> buffer{};
  Bytes payload(JW2_MAX_PAYLOAD_LEN, 0xC3);
  EXPECT_EQ(jw2_encode_frame(
                buffer.data(), 64, UINT32_MAX, UINT32_MAX, 3, 0, payload.data(), payload.size()),
            64);
  jw2_frame_t decoded;
  ASSERT_EQ(jw2_decode_frame(buffer.data(), 64, &decoded), 0);
  EXPECT_EQ(decoded.session_id, UINT32_MAX);
  EXPECT_EQ(decoded.message_id, UINT32_MAX);
  EXPECT_EQ(decoded.payload_len, JW2_MAX_PAYLOAD_LEN);
  for (size_t len = 0; len < 64; ++len) {
    EXPECT_EQ(jw2_decode_frame(buffer.data(), len, &decoded), -1);
  }
  EXPECT_EQ(jw2_decode_frame(buffer.data(), 65, &decoded), -1);
  EXPECT_EQ(jw2_decode_frame(nullptr, 64, &decoded), -1);
  EXPECT_EQ(jw2_decode_frame(buffer.data(), 64, nullptr), -1);
  EXPECT_EQ(jw2_encode_frame(buffer.data(), 63, 1, 1, 3, 0, payload.data(), payload.size()), -1);
  EXPECT_EQ(jw2_encode_frame(buffer.data(), 65, 1, 1, 3, 0, payload.data(), 50), -1);
  EXPECT_EQ(jw2_encode_frame(buffer.data(), 64, 0, 1, 3, 0, nullptr, 0), -1);
  EXPECT_EQ(jw2_encode_frame(buffer.data(), 64, 1, 0, 3, 0, nullptr, 0), -1);
  EXPECT_EQ(jw2_encode_frame(buffer.data(), 64, 1, 1, 3, 0, nullptr, 1), -1);
  EXPECT_EQ(jw2_encode_frame(nullptr, 64, 1, 1, 3, 0, nullptr, 0), -1);
  auto v2 = Request(1, 1, JW_CMD_ENABLE, 0);
  jw1_frame_t legacy;
  EXPECT_EQ(jw1_decode_frame(v2.data(), v2.size(), &legacy), -1);
  const int v1_len = jw1_encode_enable(buffer.data(), buffer.size(), 0);
  EXPECT_EQ(jw2_decode_frame(buffer.data(), v1_len, &decoded), -1);
}

TEST(JoshuaWireV2, ResponseCopiesAndMatchesAllCorrelationFields) {
  const auto bytes = Request(44, 91, JW_CMD_ENABLE, 2);
  jw2_frame_t request;
  ASSERT_EQ(jw2_decode_frame(bytes.data(), bytes.size(), &request), 0);
  uint8_t response[JW2_MAX_FRAME_LEN];
  const uint8_t ok = 0;
  const int len = jw2_encode_response(response, sizeof(response), &request, &ok, 1);
  jw2_frame_t decoded;
  ASSERT_EQ(jw2_decode_frame(response, len, &decoded), 0);
  EXPECT_TRUE(jw2_response_matches(&request, &decoded));
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
    EXPECT_FALSE(jw2_response_matches(&request, &wrong));
  }
  EXPECT_FALSE(jw2_response_matches(nullptr, &decoded));
  EXPECT_EQ(jw2_encode_response(response, sizeof(response), nullptr, &ok, 1), -1);
}

class FirmwareSessionTest : public ::testing::Test {
 protected:
  struct Device {
    int calls = 0;
    int resets = 0;
    bool enabled = false;
    bool fail = false;
  } device;
  jw2_firmware_session_t session{};
  Bytes response;

  static int Handle(void* context, const jw2_frame_t* request, uint8_t* payload, size_t) {
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
    response.resize(JW2_MAX_FRAME_LEN);
    const int len = jw2_firmware_session_process(&session,
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
                   0x00,
                   0x8c,
                   0x43}));
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
  jw2_firmware_session_init(&session);  // Reboot clears session and retained response.
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
  return jw_encode_status_payload(out, cap, JW_STATUS_OK);
}
void NoopReset(void*) {}

TEST(JoshuaWireSerialEndpoint, ArtifactVersionIsExplicitAndV1BytesStayIdentical) {
  uint8_t request[JW1_MAX_FRAME_LEN];
  uint8_t response[JW2_MAX_FRAME_LEN];
  const int request_len = jw1_encode_enable(request, sizeof(request), 0);
  uint8_t expected[JW1_MAX_FRAME_LEN];
  const int expected_len =
      jw1_encode_status_response(expected, sizeof(expected), 5, 0, JW_STATUS_OK);
  jw_serial_endpoint_t endpoint;
  jw_serial_endpoint_init(&endpoint, 1);
  const int len = jw_serial_endpoint_process(&endpoint,
                                             request,
                                             request_len,
                                             response,
                                             sizeof(response),
                                             NeutralHandler,
                                             NoopReset,
                                             nullptr);
  ASSERT_EQ(len, expected_len);
  EXPECT_EQ(Bytes(response, response + len), Bytes(expected, expected + expected_len));
  const auto v2 = Request(1, 1, JW_CMD_RESET_SESSION);
  EXPECT_EQ(jw_serial_endpoint_process(&endpoint,
                                       v2.data(),
                                       v2.size(),
                                       response,
                                       sizeof(response),
                                       NeutralHandler,
                                       NoopReset,
                                       nullptr),
            0);
  jw_serial_endpoint_init(&endpoint, 2);
  EXPECT_EQ(jw_serial_endpoint_process(&endpoint,
                                       request,
                                       request_len,
                                       response,
                                       sizeof(response),
                                       NeutralHandler,
                                       NoopReset,
                                       nullptr),
            0);
}
}  // namespace
