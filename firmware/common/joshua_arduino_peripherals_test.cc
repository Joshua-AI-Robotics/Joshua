// Actual Arduino adapter + shared runtime/endpoint; only native I/O is replaced.
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>

#include <array>
#include <cstring>

#include "firmware/common/joshua_peripheral_device.h"
#include "firmware/common/joshua_stepdir_device.h"
#include "gtest/gtest.h"
ChannelState g_channels[2];
jw_runtime_function_t g_functions[2];
jw_resource_claim_t g_stepdir_resources[6];
const uint8_t g_num_channels = 2;
int pin_values[256]{};
int pin_writes = 0;
unsigned long clock_us = 0;
namespace {
unsigned duties[256]{}, ledc_pins[8]{}, frequencies[256]{};
}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t p, uint8_t v) {
  pin_values[p] = v;
  ++pin_writes;
}
int digitalRead(uint8_t p) {
  return pin_values[p];
}
unsigned long micros() {
  return clock_us;
}
unsigned long millis() {
  return clock_us / 1000;
}
void analogWriteResolution(unsigned) {}
void analogWriteFrequency(uint8_t p, unsigned f) {
  frequencies[p] = f;
}
void analogWrite(uint8_t p, unsigned d) {
  duties[p] = d;
}
double ledcSetup(unsigned, unsigned f, unsigned) {
  return f;
}
void ledcAttachPin(uint8_t p, unsigned i) {
  ledc_pins[i] = p;
}
void ledcWrite(unsigned i, unsigned d) {
  duties[ledc_pins[i]] = d;
}
void ledcDetachPin(uint8_t) {}
namespace {
using Bytes = std::vector<uint8_t>;
constexpr bool teensy = EXPECTED_NATIVE_BOARD == 1;
class ArduinoPeripheralTest : public ::testing::Test {
 protected:
  uint32_t message = 0;
  std::array<uint8_t, 96> out{};
  void SetUp() override {
    std::fill(std::begin(duties), std::end(duties), 0);
    Wire = TwoWire();
    Wire1 = TwoWire();
    Wire2 = TwoWire();
    Serial1 = HardwareSerial();
    SPI = SPIClass();
    SPI1 = SPIClass();
    StepDirDeviceInit(33, "adapter-test", [](uint8_t p) { return p < 40; });
    ASSERT_TRUE(JoshuaArduinoPeripheralsInit());
    ASSERT_EQ(Core(JW_CMD_RESET_SESSION, {}).status, JW_STATUS_OK);
  }
  jw_reply_t Core(uint8_t op, const Bytes& payload, uint8_t channel = 255) {
    uint8_t request[96];
    const int n = jw_encode_frame(
        request, sizeof(request), 44, ++message, op, channel, payload.data(), payload.size());
    const int size = StepDirDeviceProcess(request, n, out.data(), out.size());
    EXPECT_GT(size, 0);
    jw_frame_t frame{};
    EXPECT_EQ(jw_decode_frame(out.data(), size, &frame), JW_RESULT_OK);
    jw_reply_t reply{};
    EXPECT_EQ(jw_decode_reply_payload(frame.payload, frame.payload_len, &reply), JW_RESULT_OK);
    return reply;
  }
  jw_reply_t Call(unsigned slot, unsigned op, const Bytes& data = {}) {
    Bytes payload(81);
    const jw_profile_request_t q{
        JW_PROFILE_PERIPHERAL, 1, 0, static_cast<uint8_t>(op), data.data(), data.size()};
    payload.resize(jw_encode_profile_request(payload.data(), payload.size(), &q));
    return Core(JW_CMD_PROFILE, payload, 192 + slot);
  }
  void Configure(unsigned slot, const jw_peripheral_config_t& c) {
    Bytes bytes(18);
    ASSERT_EQ(jw_encode_peripheral_config(bytes.data(), bytes.size(), &c), 18);
    ASSERT_EQ(Call(slot, 0, bytes).status, JW_STATUS_OK);
  }
};
TEST_F(ArduinoPeripheralTest, PwmIsDisabledUntilEnabledAndEstopDisablesBeforeRelease) {
  const uint8_t pin = teensy ? 6 : 4;
  Configure(0, {JW_PERIPHERAL_PWM, 0, {pin, 255, 255, 255}, 1000, 32768, 0, 0});
  EXPECT_EQ(duties[pin], 0u);
  ASSERT_EQ(Call(0, JW_PERIPHERAL_WRITE, {0xff, 0xff}).status, JW_STATUS_OK);
  EXPECT_EQ(duties[pin], 0u);
  ASSERT_EQ(Call(0, JW_PERIPHERAL_ENABLE).status, JW_STATUS_OK);
  EXPECT_EQ(duties[pin], 4095u);
  ASSERT_EQ(Core(JW_CMD_ESTOP, {0, 0}).status, JW_STATUS_OK);
  EXPECT_EQ(duties[pin], 0u);
}
TEST_F(ArduinoPeripheralTest, SpiModeTranslationAndI2cRepeatedStartUseNativeApis) {
  const uint8_t clk = teensy ? 13 : 18, mosi = teensy ? 11 : 23, miso = teensy ? 12 : 19,
                cs = teensy ? 10 : 13;
  Configure(0, {JW_PERIPHERAL_SPI, 0, {clk, mosi, miso, cs}, 1000000, 3, 0, 0});
  const auto spi = Call(0, JW_PERIPHERAL_TRANSFER, {JW_PERIPHERAL_SPI, 2, 2, 1, 2});
  ASSERT_EQ(spi.status, JW_STATUS_OK);
  EXPECT_EQ(Bytes(spi.data, spi.data + spi.data_len), (Bytes{2, 0x81, 0x82}));
  if (teensy) EXPECT_EQ(SPI.mode, SPI_MODE3);
  const uint8_t sda = teensy ? 18 : 21, scl = teensy ? 19 : 22;
  Configure(1,
            {JW_PERIPHERAL_I2C,
             0,
             {sda, scl, 255, 255},
             100000,
             0x48,
             static_cast<uint16_t>(teensy ? 50 : 5),
             0});
  ASSERT_EQ(Call(1, JW_PERIPHERAL_TRANSFER, {JW_PERIPHERAL_I2C, 1, 2, 7}).status, JW_STATUS_OK);
  EXPECT_FALSE(Wire.stop);
  EXPECT_TRUE(Wire.request_stop);
  EXPECT_EQ(Wire.writes, (Bytes{7}));
  Wire.short_read = true;
  EXPECT_EQ(Call(1, JW_PERIPHERAL_TRANSFER, {JW_PERIPHERAL_I2C, 0, 2}).status,
            JW_STATUS_OUTCOME_UNKNOWN);
  EXPECT_TRUE(StepDirDeviceRuntime()->estopped);
}
TEST_F(ArduinoPeripheralTest, UartPollsWithoutWaitingAndDigitalUsesLogicalPolarity) {
  const uint8_t tx = teensy ? 1 : 17, rx = teensy ? 0 : 16, pin = teensy ? 5 : 32;
  Configure(0, {JW_PERIPHERAL_UART, 1, {tx, rx, 255, 255}, 115200, 0, 0, 0});
  Serial1.write_capacity = 0;
  EXPECT_EQ(Call(0, JW_PERIPHERAL_TRANSFER, {JW_PERIPHERAL_UART, 1, 0, 3}).status, JW_STATUS_BUSY);
  EXPECT_TRUE(Serial1.output.empty());
  Serial1.input = {4, 5};
  auto read = Call(0, JW_PERIPHERAL_TRANSFER, {JW_PERIPHERAL_UART, 0, 3});
  ASSERT_EQ(read.status, JW_STATUS_OK);
  EXPECT_EQ(Bytes(read.data, read.data + read.data_len), (Bytes{2, 4, 5}));
  read = Call(0, JW_PERIPHERAL_TRANSFER, {JW_PERIPHERAL_UART, 0, 3});
  EXPECT_EQ(read.data_len, 1u);
  EXPECT_EQ(read.data[0], 0);
  Configure(1, {JW_PERIPHERAL_DIGITAL, 0, {pin, 255, 255, 255}, 0, 0, 0, 3});
  EXPECT_EQ(pin_values[pin], 1);
  ASSERT_EQ(Call(1, JW_PERIPHERAL_WRITE, {1}).status, JW_STATUS_OK);
  EXPECT_EQ(pin_values[pin], 0);
  EXPECT_EQ(Call(1, JW_PERIPHERAL_READ).data[0], 1);
  ASSERT_EQ(Core(JW_CMD_ESTOP, {0, 0}).status, JW_STATUS_OK);
  EXPECT_EQ(pin_values[pin], 1);
  EXPECT_FALSE(Serial1.started);
}
}  // namespace
