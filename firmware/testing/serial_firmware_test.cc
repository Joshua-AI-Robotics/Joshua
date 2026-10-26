// Shared native test suite compiled for Teensy/ESP32 and JoshuaWire.
// Calls the actual setup/loop and serial/STEP_DIR code with fake Arduino I/O
// to verify dispatch, frame rejection, retry behavior and safe reset/ESTOP.
#include <Arduino.h>

#include <cstring>
#include <vector>

#include "firmware/common/joshua_wire_endpoint.h"
#include "firmware/common/joshua_wire_profiles.h"
#include "firmware/common/serial_frame_transport.h"
#include "firmware/common/stepdir_channel.h"
#include "gtest/gtest.h"

ChannelState g_channels[2];
jw_runtime_function_t g_functions[2];
jw_resource_claim_t g_stepdir_resources[6];
const uint8_t g_num_channels = 2;
int pin_values[256]{};
int pin_writes = 0;
unsigned long clock_us = 0;
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t value) {
  pin_values[pin] = value;
  ++pin_writes;
}
unsigned long micros() {
  return clock_us;
}
unsigned long millis() {
  return clock_us / 1000;
}

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
    Serial.write_capacity = JW_MAX_FRAME_LEN;
    clock_us = 0;
    memset(g_channels, 0, sizeof(ChannelState) * g_num_channels);
    pin_writes = 0;
    setup();
    ASSERT_EQ(Send(JW_CMD_RESET_SESSION, 0xff), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  }
  Bytes Frame(uint8_t cmd, uint8_t channel, const Bytes& payload = {}) {
    const Bytes body =
        (cmd == JW_CMD_ESTOP || cmd == JW_CMD_RECOVER) && payload.empty() ? Bytes{0, 0} : payload;
    Bytes bytes(JW_MAX_FRAME_LEN);
    const int len = jw_encode_frame(
        bytes.data(), bytes.size(), session, ++id, cmd, channel, body.data(), body.size());
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
    jw_frame_t sent;
    jw_frame_t reply;
    EXPECT_EQ(jw_decode_frame(request.data(), request.size(), &sent), JW_RESULT_OK);
    if (jw_decode_frame(response.data(), response.size(), &reply) != JW_RESULT_OK) {
      ADD_FAILURE() << "Invalid firmware response";
      return {};
    }
    EXPECT_TRUE(jw_response_matches(&sent, &reply));
    return Bytes(reply.payload, reply.payload + reply.payload_len);
  }
  Bytes ProfileFrame(uint8_t op, uint8_t channel, const Bytes& data = {}) {
    uint8_t payload[JW_MAX_PAYLOAD_LEN];
    jw_profile_request_t request{JW_PROFILE_ACTUATOR, 1, 0, op, data.data(), data.size()};
    int len = jw_encode_profile_request(payload, sizeof(payload), &request);
    return Frame(JW_CMD_PROFILE, channel, Bytes(payload, payload + len));
  }
  Bytes SendProfile(uint8_t op, uint8_t channel, const Bytes& data = {}) {
    uint8_t payload[JW_MAX_PAYLOAD_LEN];
    jw_profile_request_t request{JW_PROFILE_ACTUATOR, 1, 0, op, data.data(), data.size()};
    int len = jw_encode_profile_request(payload, sizeof(payload), &request);
    return Send(JW_CMD_PROFILE, channel, Bytes(payload, payload + len));
  }
  Bytes ConfigPayload(uint8_t step_pin = 2, uint8_t dir_pin = 5, uint8_t enable_pin = 4) {
    jw_configure_step_dir_t config{};
    config.max_pulse_rate_hz = 1000;
    config.enable_active_low = true;
    config.step_pin = step_pin;
    config.dir_pin = dir_pin;
    config.enable_pin = enable_pin;
    uint8_t encoded[JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN];
    const int len = jw_encode_configure_step_dir_payload(encoded, sizeof(encoded), &config);
    return Bytes(encoded, encoded + len);
  }
};

TEST_F(SerialFirmwareTest, AllCommandsUseActualFirmwareDispatch) {
  const auto identity = Send(JW_CMD_IDENTIFY, 0xff);
  ASSERT_EQ(identity.size(), JW_IDENTIFY_PAYLOAD_LEN + JW_REPLY_HEADER_LEN);
  jw_identify_response_t value{};
  ASSERT_EQ(jw_decode_identify_payload(identity.data() + 2, identity.size() - 2, &value),
            JW_RESULT_OK);
  EXPECT_EQ(value.protocol_min, JW_PROTO_VERSION);
  EXPECT_EQ(value.function_count, g_num_channels);
  EXPECT_EQ(SendProfile(JW_ACTUATOR_ENABLE, 0),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_TRUE(g_channels[0].configured);
  EXPECT_EQ(SendProfile(JW_ACTUATOR_ENABLE, 0), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_TRUE(g_channels[0].enabled);
  EXPECT_EQ(pin_values[4], LOW);
  EXPECT_EQ(SendProfile(JW_ACTUATOR_SET_TARGET, 0, {JW_MODE_POSITION, 0, 0, 0x48, 0x41}),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_FLOAT_EQ(g_channels[0].target_value, 12.5f);
  EXPECT_EQ(SendProfile(JW_ACTUATOR_GET_FEEDBACK, 0).size(),
            JW_FEEDBACK_RESPONSE_PAYLOAD_LEN + JW_REPLY_HEADER_LEN);
  EXPECT_EQ(SendProfile(JW_ACTUATOR_DISABLE, 0), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_EQ(Send(0x99, 0), (Bytes{JW_STATUS_UNSUPPORTED, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_GET_FEEDBACK, 99),
            (Bytes{JW_STATUS_INVALID_CHANNEL, JW_EXECUTION_REJECTED}));
}

TEST_F(SerialFirmwareTest, RejectsOtherVersionBadCrcAndTruncatedInput) {
  auto bytes = Frame(JW_CMD_IDENTIFY, 0xff);
  auto corrupted = bytes;
  corrupted.back() ^= 1;
  EXPECT_TRUE(Run(corrupted).empty());
  corrupted = bytes;
  corrupted.resize(5);
  EXPECT_TRUE(Run(corrupted).empty());
  clock_us += 21000;  // Let the incomplete link frame expire before resync.
  // Independent legacy ENABLE fixture: valid v1 CRC, rejected by JW.
  EXPECT_TRUE(Run(Bytes{0xa5, 0x03, 0x01, 0x05, 0x00, 0x59, 0x04}).empty());
  EXPECT_FALSE(Run(bytes).empty());
}

TEST_F(SerialFirmwareTest, DuplicateResetAndEstopDoNotRestartMotion) {
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  const auto enable = ProfileFrame(JW_ACTUATOR_ENABLE, 0);
  const auto response = Run(enable);
  const auto writes = pin_writes;
  EXPECT_EQ(Run(enable), response);
  EXPECT_EQ(pin_writes, writes);
  EXPECT_EQ(Send(JW_CMD_ESTOP, 0xff), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_ENABLE, 0),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  EXPECT_FALSE(g_channels[0].enabled);
  ++session;
  id = 0;
  EXPECT_EQ(Send(JW_CMD_RESET_SESSION, 0xff), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_FALSE(g_channels[0].configured);
  EXPECT_EQ(pin_values[4], HIGH);
  EXPECT_TRUE(Run(enable).empty());
  EXPECT_EQ(SendProfile(JW_ACTUATOR_ENABLE, 0),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(Send(JW_CMD_RECOVER, 0xff), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_ENABLE, 0), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
}

TEST_F(SerialFirmwareTest, ResetWhileEnabledDisablesOldPinsBeforeForgettingThem) {
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_ENABLE, 0), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  ++session;
  id = 0;
  EXPECT_EQ(Send(JW_CMD_RESET_SESSION, 0xff), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(pin_values[4], HIGH);
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_FALSE(g_channels[0].configured);
}

TEST_F(SerialFirmwareTest, RetainsPartialInputAndServicesMotorsBetweenBytes) {
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_ENABLE, 0), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_SET_TARGET, 0, {JW_MODE_POSITION, 0, 0, 0x20, 0x41}),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  const auto request = Frame(JW_CMD_IDENTIFY, 0xff);
  for (size_t i = 0; i < request.size(); ++i) {
    clock_us += 1000;
    loop();
    clock_us += 1000;
    const auto reply = Run({request[i]});
    if (i + 1 < request.size())
      EXPECT_TRUE(reply.empty());
    else
      EXPECT_FALSE(reply.empty());
  }
  EXPECT_EQ(g_channels[0].step_dir.position_steps, 10);
}

TEST_F(SerialFirmwareTest, BlockedResponseDoesNotRepeatCommandOrConsumeNextRequest) {
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_EQ(SendProfile(JW_ACTUATOR_ENABLE, 0), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  Serial.write_capacity = 0;
  // Fill the adapter TX slot before producing a response retained by main.
  EXPECT_TRUE(Run(Frame(JW_CMD_IDENTIFY, 0xff)).empty());
  const auto target = ProfileFrame(JW_ACTUATOR_SET_TARGET, 0, {JW_MODE_POSITION, 0, 0, 0x20, 0x41});
  EXPECT_TRUE(Run(target).empty());
  ASSERT_FLOAT_EQ(g_channels[0].target_value, 10);
  // Any repeated command dispatch would restore 10. Motor servicing must still
  // run while TX is blocked, and the next request must remain in the RX queue.
  g_channels[0].target_value = 20;
  const auto next = ProfileFrame(JW_ACTUATOR_DISABLE, 0);
  EXPECT_TRUE(Run(next).empty());
  for (int i = 0; i < 3; ++i) {
    clock_us += 1000;
    loop();
    clock_us += 1000;
    loop();
  }
  EXPECT_FLOAT_EQ(g_channels[0].target_value, 20);
  EXPECT_EQ(g_channels[0].step_dir.position_steps, 3);
  EXPECT_TRUE(g_channels[0].enabled);
  EXPECT_EQ(Serial.input.size(), next.size());
  // Insufficient space must never emit the first part of a response.
  Serial.write_capacity = JW_FRAME_OVERHEAD;
  loop();
  EXPECT_TRUE(Serial.output.empty());
  Serial.write_capacity = JW_MAX_FRAME_LEN;
  loop();
  jw_frame_t response;
  ASSERT_GE(Serial.output.size(), 2);
  const size_t first_length = Serial.output[1] + 4;
  ASSERT_LT(first_length, Serial.output.size());
  ASSERT_EQ(
      jw_decode_frame(
          Serial.output.data() + first_length, Serial.output.size() - first_length, &response),
      JW_RESULT_OK);
  EXPECT_EQ(response.message_id, id - 1);
  EXPECT_EQ(response.cmd, JW_CMD_PROFILE);
  EXPECT_EQ(Serial.input.size(), next.size());
  Serial.output.clear();
  loop();
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_TRUE(Serial.input.empty());
  ASSERT_EQ(jw_decode_frame(Serial.output.data(), Serial.output.size(), &response), JW_RESULT_OK);
  EXPECT_EQ(response.message_id, id);
}

TEST_F(SerialFirmwareTest, BoundsNoiseDrainingPerMotorServiceIteration) {
  Run(Bytes(JW_MAX_FRAME_LEN * 3, 0x00));
  EXPECT_EQ(Serial.input.size(), JW_MAX_FRAME_LEN * 2);
  EXPECT_TRUE(Serial.output.empty());
}

TEST_F(SerialFirmwareTest, BlockedTxDuplicateAndEstopStopWithinFaultBudget) {
  SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload());
  SendProfile(JW_ACTUATOR_ENABLE, 0);
  SendProfile(JW_ACTUATOR_CONFIGURE, 1, ConfigPayload(12, 13, 14));
  SendProfile(JW_ACTUATOR_ENABLE, 1);
  ASSERT_TRUE(g_channels[0].enabled);
  ASSERT_TRUE(g_channels[1].enabled);
  Serial.write_capacity = 0;
  const auto identify = Frame(JW_CMD_IDENTIFY, 0xff);
  EXPECT_TRUE(Run(identify).empty());
  EXPECT_TRUE(Run(identify).empty());  // Duplicate fills the application reply slot.
  const auto estop = Frame(JW_CMD_ESTOP, 0xff);
  EXPECT_TRUE(Run(estop).empty());
  for (uint32_t i = 0; i < JW_SERIAL_TX_STOP_TIMEOUT_MS - 1; ++i) {
    clock_us += 1000;
    loop();
  }
  EXPECT_TRUE(g_channels[0].enabled);
  clock_us += 1000;
  loop();
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_FALSE(g_channels[0].configured);
  EXPECT_FALSE(g_channels[1].enabled);
  EXPECT_FALSE(g_channels[1].configured);
  EXPECT_EQ(pin_values[4], HIGH);
  EXPECT_EQ(pin_values[14], HIGH);
  EXPECT_EQ(pin_values[2], LOW);
  EXPECT_EQ(pin_values[12], LOW);
  EXPECT_EQ(Serial.input.size(), estop.size());
  EXPECT_EQ(g_functions[0].state.reason, JW_REASON_DEVICE_FAULT);
  EXPECT_EQ(g_functions[0].state.stop_phase, JW_STOP_ACTION_COMPLETE);
  EXPECT_EQ(g_functions[0].state.verification, JW_SAFE_UNSUPPORTED);
  EXPECT_EQ(g_functions[0].state.action_message_id, 0);

  // Both copies of the IDENTIFY reply retain their exact correlation and bytes.
  Serial.write_capacity = JW_MAX_FRAME_LEN;
  loop();
  ASSERT_EQ(Serial.output.size() % 2, 0);
  const size_t length = Serial.output.size() / 2;
  jw_frame_t request, reply;
  ASSERT_EQ(jw_decode_frame(identify.data(), identify.size(), &request), JW_RESULT_OK);
  ASSERT_EQ(jw_decode_frame(Serial.output.data(), length, &reply), JW_RESULT_OK);
  EXPECT_TRUE(jw_response_matches(&request, &reply));
  EXPECT_EQ(Bytes(Serial.output.begin(), Serial.output.begin() + length),
            Bytes(Serial.output.begin() + length, Serial.output.end()));
  Serial.output.clear();
  loop();  // The queued ESTOP is now dispatched normally.
  ASSERT_EQ(jw_decode_frame(Serial.output.data(), Serial.output.size(), &reply), JW_RESULT_OK);
  EXPECT_EQ(reply.cmd, JW_CMD_ESTOP);
  EXPECT_EQ(reply.message_id, id);
  EXPECT_FALSE(g_channels[0].enabled);
}

TEST_F(SerialFirmwareTest, TxFaultNeedsExplicitRecoveryAndNeverReexecutesCachedEnable) {
  SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload());
  Serial.write_capacity = 0;
  const auto enable = ProfileFrame(JW_ACTUATOR_ENABLE, 0);
  EXPECT_TRUE(Run(enable).empty());
  ASSERT_TRUE(g_channels[0].enabled);
  clock_us += JW_SERIAL_TX_STOP_TIMEOUT_MS * 1000;
  loop();  // No second request or ESTOP is needed to trigger the local fault.
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_EQ(g_functions[0].state.reason, JW_REASON_DEVICE_FAULT);
  Serial.write_capacity = JW_MAX_FRAME_LEN;
  loop();
  const auto cached_reply = Serial.output;
  EXPECT_EQ(Run(enable), cached_reply);
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_FALSE(g_channels[0].configured);
  // The local fault is also observable through the wire status API.
  for (const uint8_t scope : {uint8_t{0xff}, uint8_t{0}, uint8_t{1}}) {
    const auto payload = Send(JW_CMD_GET_STATUS, scope);
    ASSERT_GE(payload.size(), JW_REPLY_HEADER_LEN);
    jw_device_status_t status;
    ASSERT_EQ(
        jw_decode_device_status(
            payload.data() + JW_REPLY_HEADER_LEN, payload.size() - JW_REPLY_HEADER_LEN, &status),
        JW_RESULT_OK);
    EXPECT_EQ(status.reason, JW_REASON_DEVICE_FAULT);
    EXPECT_EQ(status.stop_phase, JW_STOP_ACTION_COMPLETE);
    EXPECT_EQ(status.recovery,
              JW_RECOVERY_CLEAR_ESTOP | JW_RECOVERY_RECONFIGURE | JW_RECOVERY_EXPLICIT_ENABLE);
    EXPECT_EQ(status.action_session_id, 0);
    EXPECT_EQ(status.action_message_id, 0);
  }
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  ++session;
  id = 0;
  Send(JW_CMD_RESET_SESSION, 0xff);
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(Send(JW_CMD_RECOVER, 0xff), (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_EQ(SendProfile(JW_ACTUATOR_ENABLE, 0),
            (Bytes{JW_STATUS_UNAVAILABLE, JW_EXECUTION_REJECTED}));
  SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload());
  EXPECT_FALSE(g_channels[0].enabled);
  SendProfile(JW_ACTUATOR_ENABLE, 0);
  EXPECT_TRUE(g_channels[0].enabled);
}
TEST_F(SerialFirmwareTest, ConflictingResourcesAndEnabledReconfigurationNeverChangePins) {
  ASSERT_GT(g_num_channels, 1);
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_OK, JW_EXECUTION_COMPLETE}));
  const auto writes = pin_writes;
  auto conflict = SendProfile(JW_ACTUATOR_CONFIGURE, 1, ConfigPayload());
  ASSERT_GE(conflict.size(), 2);
  EXPECT_EQ(conflict[0], JW_STATUS_RESOURCE_CONFLICT);
  EXPECT_EQ(pin_writes, writes);
  EXPECT_FALSE(g_channels[1].configured);
  EXPECT_TRUE(g_channels[0].configured);
  SendProfile(JW_ACTUATOR_ENABLE, 0);
  const auto enabled_writes = pin_writes;
  EXPECT_EQ(SendProfile(JW_ACTUATOR_CONFIGURE, 0, ConfigPayload()),
            (Bytes{JW_STATUS_BUSY, JW_EXECUTION_REJECTED}));
  EXPECT_EQ(pin_writes, enabled_writes);
  EXPECT_TRUE(g_channels[0].enabled);
}
}  // namespace
