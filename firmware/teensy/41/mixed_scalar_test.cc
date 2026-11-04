// Builds the actual Teensy setup/loop/ADC register adapter against memory hooks.
#include <Arduino.h>

#include <vector>

#include "adc_reference.h"
#include "firmware/common/joshua_mixed_device.h"
#include "firmware/common/joshua_wire_stream.h"
#include "gtest/gtest.h"

ChannelState g_channels[1];
jw_runtime_function_t g_functions[1];
jw_resource_claim_t g_stepdir_resources[3];
const uint8_t g_num_channels = 1;
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
class TeensyMixedTest : public ::testing::Test {
 protected:
  uint32_t message = 0;
  void SetUp() override {
    clock_us = 0;
    Serial.input.clear();
    Serial.output.clear();
    Serial.write_capacity = 1000;
    ADC1_GC = ADC1_GS = ADC1_HS = 0;
    ADC1_R0 = 512;
    setup();
    Request(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE);
  }
  Bytes Request(uint8_t command, uint8_t function, Bytes payload = {}) {
    Bytes bytes(JW_MAX_FRAME_LEN);
    bytes.resize(jw_encode_frame(bytes.data(),
                                 bytes.size(),
                                 10,
                                 ++message,
                                 command,
                                 function,
                                 payload.data(),
                                 payload.size()));
    for (auto byte : bytes) Serial.input.push_back(byte);
    Serial.output.clear();
    loop();
    loop();
    return Bytes(Serial.output.begin(), Serial.output.end());
  }
  void ConfigurePolicy() {
    jw_watchdog_policy_t policy{500000, 100000};
    Bytes data(8);
    jw_encode_watchdog_policy(data.data(), data.size(), &policy);
    auto bytes = Request(JW_CMD_CONFIGURE_WATCHDOGS, JW_CHANNEL_NONE, data);
    jw_frame_t frame{};
    ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
  }
  void Subscribe() {
    ConfigurePolicy();
    Bytes period(4);
    jw_encode_stream_period(period.data(), period.size(), 10000);
    Bytes payload(9);
    const jw_profile_request_t profile{
        JW_PROFILE_SENSOR, 1, 1, JW_SENSOR_SUBSCRIBE, period.data(), period.size()};
    jw_encode_profile_request(payload.data(), payload.size(), &profile);
    auto bytes = Request(JW_CMD_PROFILE, 128, payload);
    jw_frame_t frame{};
    ASSERT_EQ(jw_decode_frame(bytes.data(), bytes.size(), &frame), JW_RESULT_OK);
    jw_reply_t reply{};
    ASSERT_EQ(jw_decode_reply_payload(frame.payload, frame.payload_len, &reply), JW_RESULT_OK);
    ASSERT_EQ(reply.status, JW_STATUS_OK);
  }
};
TEST_F(TeensyMixedTest, ActualRegisterAdapterStartsPollsCancelsWithoutWaiting) {
  auto acquisition = ReferenceAnalogAcquisition();
  ADC1_GC = ADC_GC_CAL;
  EXPECT_FALSE(acquisition.start(nullptr));
  ADC1_GC = 0;
  ASSERT_TRUE(acquisition.start(nullptr));
  EXPECT_EQ(ADC1_HC0, 7);
  float value = 0;
  ADC1_HS = 0;
  EXPECT_EQ(acquisition.poll(nullptr, &value), 0);
  ADC1_HS = ADC_HS_COCO0;
  ADC1_R0 = 512;
  EXPECT_EQ(acquisition.poll(nullptr, &value), 1);
  EXPECT_EQ(value, 512);
  acquisition.cancel(nullptr);
  EXPECT_EQ(ADC1_HC0, 31);
}
TEST_F(TeensyMixedTest, SetupLoopEmitTheRealScalarNotification) {
  Subscribe();
  Serial.output.clear();
  clock_us = 100;
  ADC1_HS = ADC_HS_COCO0;
  loop();
  loop();
  Bytes bytes(Serial.output.begin(), Serial.output.end());
  jw_stream_sample_t sample{};
  ASSERT_EQ(jw_decode_stream_sample(bytes.data(), bytes.size(), &sample), JW_RESULT_OK);
  EXPECT_EQ(sample.function_id, 128);
  EXPECT_EQ(sample.sample.value, 512);
  EXPECT_EQ(sample.sample.metadata.validity, JW_SAMPLE_VALID);
  EXPECT_EQ(sample.sample.metadata.clock, JW_CLOCK_BOOT_MONOTONIC_US);
}
TEST_F(TeensyMixedTest, StalledConversionDoesNotPreventEstopAdmission) {
  Subscribe();
  ADC1_HS = 0;
  clock_us = 1001;
  loop();
  auto bytes = Request(JW_CMD_ESTOP, JW_CHANNEL_NONE, {0, 0});
  // Invalid sample traffic may precede the response, so frame incrementally.
  jw_stream_decoder_t decoder;
  jw_stream_init(&decoder);
  bool saw_stop = false;
  for (auto byte : bytes) {
    const int size = jw_stream_feed(&decoder, byte, clock_us / 1000);
    if (size > 0) {
      jw_frame_t frame{};
      jw_decode_frame(decoder.bytes, size, &frame);
      if (frame.cmd == JW_CMD_ESTOP) saw_stop = true;
    }
  }
  EXPECT_TRUE(saw_stop);
  EXPECT_TRUE(StepDirDeviceRuntime()->estopped);
  EXPECT_EQ(ADC1_HC0, 31);
}
}  // namespace
