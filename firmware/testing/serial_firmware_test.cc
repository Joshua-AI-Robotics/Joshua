// Shared native test suite compiled for Teensy/ESP32 and each wire version.
// Calls the actual setup/loop and serial/STEP_DIR code with fake Arduino I/O
// to verify dispatch, frame rejection, retry behavior and safe reset/ESTOP.
#include <Arduino.h>

#include <cstring>
#include <vector>

#include "channel_table.h"
#include "firmware/common/joshua_wire_serial_endpoint.h"
#include "gtest/gtest.h"

void setup();
void loop();

namespace {
using Bytes = std::vector<uint8_t>;
class SerialFirmwareTest : public ::testing::Test {
 protected:
  uint32_t id = 0;
  uint32_t session = 100;
  void SetUp() override {
    Serial.input.clear();
    Serial.output.clear();
    memset(g_channels, 0, sizeof(ChannelState) * g_num_channels);
    pin_writes = 0;
    setup();
    if (JOSHUA_WIRE_VERSION == 2) {
      ASSERT_EQ(Send(JW_CMD_RESET_SESSION, 0xff), Bytes{0});
    }
  }
  Bytes Frame(uint8_t cmd, uint8_t channel, const Bytes& payload = {}) {
    Bytes bytes(JW2_MAX_FRAME_LEN);
    const int len =
        JOSHUA_WIRE_VERSION == 2
            ? jw2_encode_frame(bytes.data(),
                               bytes.size(),
                               session,
                               ++id,
                               cmd,
                               channel,
                               payload.data(),
                               payload.size())
            : jw1_encode_frame(
                  bytes.data(), bytes.size(), cmd, channel, payload.data(), payload.size());
    EXPECT_GT(len, 0);
    bytes.resize(len);
    return bytes;
  }
  Bytes Run(const Bytes& bytes) {
    Serial.output.clear();
    Serial.input.insert(Serial.input.end(), bytes.begin(), bytes.end());
    loop();
    return Serial.output;
  }
  Bytes Send(uint8_t cmd, uint8_t channel, const Bytes& payload = {}) {
    const auto request = Frame(cmd, channel, payload);
    const auto response = Run(request);
    if (JOSHUA_WIRE_VERSION == 2) {
      jw2_frame_t sent;
      jw2_frame_t reply;
      EXPECT_EQ(jw2_decode_frame(request.data(), request.size(), &sent), 0);
      if (jw2_decode_frame(response.data(), response.size(), &reply) != 0) {
        ADD_FAILURE() << "Invalid firmware response";
        return {};
      }
      EXPECT_TRUE(jw2_response_matches(&sent, &reply));
      return Bytes(reply.payload, reply.payload + reply.payload_len);
    }
    jw1_frame_t reply;
    if (jw1_decode_frame(response.data(), response.size(), &reply) != 0) {
      ADD_FAILURE() << "Invalid firmware response";
      return {};
    }
    EXPECT_EQ(reply.cmd, cmd);
    EXPECT_EQ(reply.channel, channel);
    return Bytes(reply.payload, reply.payload + reply.payload_len);
  }
  Bytes ConfigPayload() {
    jw_configure_step_dir_t config{};
    config.max_pulse_rate_hz = 1000;
    config.enable_active_low = true;
    config.step_pin = 2;
    config.dir_pin = 3;
    config.enable_pin = 4;
    uint8_t encoded[JW1_MAX_FRAME_LEN];
    const int len = jw1_encode_configure_channel_step_dir(encoded, sizeof(encoded), 0, &config);
    return Bytes(encoded + 5, encoded + len - 2);
  }
};

TEST_F(SerialFirmwareTest, AllCommandsUseActualFirmwareDispatch) {
  const auto identity = Send(JW_CMD_IDENTIFY, 0xff);
  ASSERT_EQ(identity.size(), JW_IDENTIFY_RESPONSE_PAYLOAD_LEN);
  EXPECT_EQ(identity[0], EXPECTED_BOARD_ID);
  EXPECT_EQ(Send(JW_CMD_ENABLE, 0), Bytes{JW_STATUS_ERROR});
  EXPECT_EQ(Send(JW_CMD_CONFIGURE_CHANNEL, 0, ConfigPayload()), Bytes{JW_STATUS_OK});
  EXPECT_TRUE(g_channels[0].configured);
  EXPECT_EQ(Send(JW_CMD_ENABLE, 0), Bytes{JW_STATUS_OK});
  EXPECT_TRUE(g_channels[0].enabled);
  EXPECT_EQ(pin_values[4], LOW);
  EXPECT_EQ(Send(JW_CMD_SET_TARGET, 0, {0, 0, 0, 0x48, 0x41}), Bytes{JW_STATUS_OK});
  EXPECT_FLOAT_EQ(g_channels[0].target_value, 12.5f);
  EXPECT_EQ(Send(JW_CMD_GET_FEEDBACK, 0).size(), JW_FEEDBACK_RESPONSE_PAYLOAD_LEN);
  EXPECT_EQ(Send(JW_CMD_DISABLE, 0), Bytes{JW_STATUS_OK});
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_EQ(Send(0x99, 0), Bytes{JW_STATUS_UNSUPPORTED});
  EXPECT_EQ(Send(JW_CMD_GET_FEEDBACK, 99), Bytes{JW_STATUS_ERROR});
}

TEST_F(SerialFirmwareTest, RejectsOtherVersionBadCrcAndTruncatedInput) {
  auto bytes = Frame(JW_CMD_IDENTIFY, 0xff);
  auto corrupted = bytes;
  corrupted.back() ^= 1;
  EXPECT_TRUE(Run(corrupted).empty());
  corrupted = bytes;
  corrupted.resize(5);
  EXPECT_TRUE(Run(corrupted).empty());
  uint8_t other[JW2_MAX_FRAME_LEN];
  const int len =
      JOSHUA_WIRE_VERSION == 2
          ? jw1_encode_enable(other, sizeof(other), 0)
          : jw2_encode_frame(other, sizeof(other), 50, 1, JW_CMD_RESET_SESSION, 0xff, nullptr, 0);
  EXPECT_TRUE(Run(Bytes(other, other + len)).empty());
  EXPECT_FALSE(Run(bytes).empty());
}

TEST_F(SerialFirmwareTest, V2DuplicateResetAndEstopDoNotRestartMotion) {
  if (JOSHUA_WIRE_VERSION != 2) GTEST_SKIP() << "v2 session semantics";
  EXPECT_EQ(Send(JW_CMD_CONFIGURE_CHANNEL, 0, ConfigPayload()), Bytes{JW_STATUS_OK});
  const auto enable = Frame(JW_CMD_ENABLE, 0);
  const auto response = Run(enable);
  const auto writes = pin_writes;
  EXPECT_EQ(Run(enable), response);
  EXPECT_EQ(pin_writes, writes);
  EXPECT_EQ(Send(JW_CMD_ESTOP, 0xff), Bytes{JW_STATUS_OK});
  EXPECT_EQ(Send(JW_CMD_ENABLE, 0), Bytes{JW_STATUS_ERROR});
  EXPECT_EQ(Send(JW_CMD_CONFIGURE_CHANNEL, 0, ConfigPayload()), Bytes{JW_STATUS_ERROR});
  EXPECT_FALSE(g_channels[0].enabled);
  ++session;
  id = 0;
  EXPECT_EQ(Send(JW_CMD_RESET_SESSION, 0xff), Bytes{JW_STATUS_OK});
  EXPECT_FALSE(g_channels[0].configured);
  EXPECT_EQ(pin_values[4], HIGH);
  EXPECT_TRUE(Run(enable).empty());
  EXPECT_EQ(Send(JW_CMD_ENABLE, 0), Bytes{JW_STATUS_ERROR});
  EXPECT_EQ(Send(JW_CMD_CONFIGURE_CHANNEL, 0, ConfigPayload()), Bytes{JW_STATUS_OK});
  EXPECT_EQ(Send(JW_CMD_ENABLE, 0), Bytes{JW_STATUS_OK});
}

TEST_F(SerialFirmwareTest, ResetWhileEnabledDisablesOldPinsBeforeForgettingThem) {
  if (JOSHUA_WIRE_VERSION != 2) GTEST_SKIP() << "v2 session semantics";
  EXPECT_EQ(Send(JW_CMD_CONFIGURE_CHANNEL, 0, ConfigPayload()), Bytes{JW_STATUS_OK});
  EXPECT_EQ(Send(JW_CMD_ENABLE, 0), Bytes{JW_STATUS_OK});
  ++session;
  id = 0;
  EXPECT_EQ(Send(JW_CMD_RESET_SESSION, 0xff), Bytes{JW_STATUS_OK});
  EXPECT_EQ(pin_values[4], HIGH);
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_FALSE(g_channels[0].configured);
}
}  // namespace
