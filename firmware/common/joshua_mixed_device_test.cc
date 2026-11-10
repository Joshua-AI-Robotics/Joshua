// Production mixed application with controlled ADC/GPIO/clock hooks, no hardware.
#include "firmware/common/joshua_mixed_device.h"

#include <Arduino.h>

#include <cmath>
#include <cstring>
#include <deque>
#include <vector>

#include "firmware/common/joshua_mixed_serial.h"
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
namespace {
using Bytes = std::vector<uint8_t>;
class MixedDeviceTest : public ::testing::Test {
 protected:
  JoshuaMixedDevice device{};
  uint32_t message = 0, session = 77;
  int starts = 0, polls = 0, canceled = 0;
  bool adc_ready = true;
  Bytes last_request, response;
  void SetUp() override {
    clock_us = 0;
    pin_writes = 0;
    StepDirDeviceInit(7, "mixed-test", [](uint8_t pin) { return pin < 40; });
    jw_scalar_acquisition_t acquisition{
        this,
        [](void* ctx) {
          ++static_cast<MixedDeviceTest*>(ctx)->starts;
          return 1;
        },
        [](void* ctx, float* value) {
          auto* s = static_cast<MixedDeviceTest*>(ctx);
          ++s->polls;
          *value = 42;
          return s->adc_ready ? 1 : 0;
        },
        [](void* ctx) { ++static_cast<MixedDeviceTest*>(ctx)->canceled; }};
    ASSERT_TRUE(JoshuaMixedDeviceInit(&device, 14, acquisition));
    Exchange(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE);
  }
  Bytes Frame(uint8_t cmd, uint8_t function, const Bytes& payload = {}) {
    Bytes bytes(JW_MAX_FRAME_LEN);
    bytes.resize(jw_encode_frame(bytes.data(),
                                 bytes.size(),
                                 session,
                                 ++message,
                                 cmd,
                                 function,
                                 payload.data(),
                                 payload.size()));
    return bytes;
  }
  jw_reply_t Send(const Bytes& request) {
    response.resize(JW_MAX_FRAME_LEN);
    const int size =
        StepDirDeviceProcess(request.data(), request.size(), response.data(), response.size());
    EXPECT_GT(size, 0);
    response.resize(size > 0 ? size : 0);
    jw_frame_t frame{};
    jw_reply_t reply{};
    EXPECT_EQ(jw_decode_frame(response.data(), response.size(), &frame), JW_RESULT_OK);
    EXPECT_EQ(jw_decode_reply_payload(frame.payload, frame.payload_len, &reply), JW_RESULT_OK);
    return reply;
  }
  jw_reply_t Exchange(uint8_t cmd, uint8_t function, const Bytes& payload = {}) {
    last_request = Frame(cmd, function, payload);
    return Send(last_request);
  }
  jw_reply_t Profile(uint16_t profile, uint8_t operation, const Bytes& data = {}) {
    Bytes payload(JW_PROFILE_HEADER_LEN + data.size());
    jw_profile_request_t request{profile,
                                 1,
                                 static_cast<uint8_t>(profile == JW_PROFILE_SENSOR ? 1 : 0),
                                 operation,
                                 data.data(),
                                 data.size()};
    jw_encode_profile_request(payload.data(), payload.size(), &request);
    return Exchange(JW_CMD_PROFILE, profile == JW_PROFILE_SENSOR ? 128 : 0, payload);
  }
  void Policy(uint32_t comm = 500000, uint32_t target = 100000) {
    const jw_watchdog_policy_t p{comm, target};
    Bytes bytes(JW_WATCHDOG_POLICY_LEN);
    ASSERT_EQ(jw_encode_watchdog_policy(bytes.data(), bytes.size(), &p), bytes.size());
    EXPECT_EQ(Exchange(JW_CMD_CONFIGURE_WATCHDOGS, JW_CHANNEL_NONE, bytes).status, JW_STATUS_OK);
  }
  void Subscribe() {
    Policy();
    Bytes bytes(4);
    jw_encode_stream_period(bytes.data(), bytes.size(), 10000);
    ASSERT_EQ(Profile(JW_PROFILE_SENSOR, JW_SENSOR_SUBSCRIBE, bytes).status, JW_STATUS_OK);
  }
  void Configure(uint8_t step = 2) {
    jw_configure_step_dir_t config{};
    config.max_pulse_rate_hz = 500;
    config.step_pin = step;
    config.dir_pin = 3;
    config.enable_pin = 4;
    config.enable_active_low = 1;
    config.step_pulse_width_us = 20;
    Bytes bytes(JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN);
    jw_encode_configure_step_dir_payload(bytes.data(), bytes.size(), &config);
    EXPECT_EQ(Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_CONFIGURE, bytes).status, JW_STATUS_OK);
  }
  void Tick(uint32_t now) {
    clock_us = now;
    StepDirDeviceService();
  }
  void Velocity() {
    Configure();
    Policy();
    ASSERT_EQ(Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_ENABLE).status, JW_STATUS_OK);
    Bytes bytes(JW_SET_TARGET_PAYLOAD_LEN);
    jw_encode_set_target_payload(bytes.data(), bytes.size(), JW_MODE_VELOCITY, 100);
    ASSERT_EQ(Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_SET_TARGET, bytes).status, JW_STATUS_OK);
  }
};
TEST(WatchdogPolicyCodec, GoldenLittleEndianPolicyAndInvalidValues) {
  const jw_watchdog_policy_t policy{1000000, 250000};
  uint8_t bytes[8];
  ASSERT_EQ(jw_encode_watchdog_policy(bytes, sizeof(bytes), &policy), 8);
  EXPECT_EQ(Bytes(bytes, bytes + 8), (Bytes{0x40, 0x42, 0x0f, 0, 0x90, 0xd0, 3, 0}));
  jw_watchdog_policy_t invalid{0, 250000};
  EXPECT_LT(jw_encode_watchdog_policy(bytes, sizeof(bytes), &invalid), 0);
  EXPECT_EQ(jw_decode_watchdog_policy(bytes, 7, &invalid), JW_RESULT_ERROR);
}
TEST_F(MixedDeviceTest, SensorAdmissionCannotStealAnActuatorPin) {
  Configure(14);
  Policy();
  const auto writes = pin_writes;
  Bytes period(4);
  jw_encode_stream_period(period.data(), period.size(), 10000);
  EXPECT_EQ(Profile(JW_PROFILE_SENSOR, JW_SENSOR_SUBSCRIBE, period).status,
            JW_STATUS_RESOURCE_CONFLICT);
  EXPECT_FALSE(device.sensor.subscription.state.active);
  EXPECT_EQ(starts, 0);
  EXPECT_EQ(pin_writes, writes);
  EXPECT_TRUE(g_channels[0].configured);
}
TEST_F(MixedDeviceTest, RepeatedLocalFaultDoesNotInventNewStateTransitions) {
  StepDirDeviceFault();
  const auto epoch = StepDirDeviceRuntime()->state.epoch;
  StepDirDeviceFault();
  EXPECT_EQ(StepDirDeviceRuntime()->state.epoch, epoch);
}
TEST_F(MixedDeviceTest, DiscoveryAndReadUseTheActualBackgroundAcquisition) {
  Subscribe();
  EXPECT_EQ(starts, 0);
  Tick(0);
  EXPECT_EQ(starts, 1);
  EXPECT_EQ(polls, 0);
  EXPECT_EQ(Profile(JW_PROFILE_SENSOR, JW_SENSOR_READ).status, JW_STATUS_NOT_READY);
  Tick(100);
  EXPECT_EQ(polls, 1);
  auto reply = Profile(JW_PROFILE_SENSOR, JW_SENSOR_READ);
  ASSERT_EQ(reply.status, JW_STATUS_OK);
  jw_sensor_sample_t sample{};
  ASSERT_EQ(jw_decode_sensor_sample_payload(
                reply.data, reply.data_len, JW_SENSOR_INTERFACE_SCALAR_F32, &sample),
            JW_RESULT_OK);
  EXPECT_EQ(sample.value, 42);
  EXPECT_EQ(sample.metadata.boot_id, 7);
  EXPECT_EQ(sample.metadata.sequence, 1);
  const auto sequence = sample.metadata.sequence;
  reply = Profile(JW_PROFILE_SENSOR, JW_SENSOR_READ);
  ASSERT_EQ(jw_decode_sensor_sample_payload(
                reply.data, reply.data_len, JW_SENSOR_INTERFACE_SCALAR_F32, &sample),
            JW_RESULT_OK);
  EXPECT_EQ(sample.metadata.sequence, sequence);
  EXPECT_EQ(starts, 1);
}
TEST_F(MixedDeviceTest, SensorPinConflictIsRejectedBeforeOutputWrites) {
  Subscribe();
  const auto writes = pin_writes;
  jw_configure_step_dir_t config{};
  config.max_pulse_rate_hz = 500;
  config.step_pin = 14;
  config.dir_pin = 3;
  config.enable_pin = 4;
  Bytes bytes(JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN);
  jw_encode_configure_step_dir_payload(bytes.data(), bytes.size(), &config);
  EXPECT_EQ(Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_CONFIGURE, bytes).status,
            JW_STATUS_RESOURCE_CONFLICT);
  EXPECT_EQ(pin_writes, writes);
  EXPECT_FALSE(g_channels[0].configured);
}
TEST_F(MixedDeviceTest, FailedConversionIsInvalidAndDoesNotBlockStop) {
  adc_ready = false;
  Subscribe();
  Tick(0);
  Tick(1001);
  EXPECT_EQ(device.sensor.latest.metadata.validity, JW_SAMPLE_INVALID);
  EXPECT_TRUE(std::isnan(device.sensor.latest.value));
  EXPECT_GE(canceled, 1);
  EXPECT_EQ(Exchange(JW_CMD_ESTOP, JW_CHANNEL_NONE, {0, 0}).status, JW_STATUS_OK);
  EXPECT_FALSE(device.sensor.subscription.state.active);
  EXPECT_FALSE(device.sensor.in_flight);
}
TEST_F(MixedDeviceTest, WatchdogsAreExplicitAndConfigurationCannotEnableOutputs) {
  Configure();
  EXPECT_EQ(Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_ENABLE).status, JW_STATUS_NOT_READY);
  Policy();
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_EQ(Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_ENABLE).status, JW_STATUS_OK);
  jw_watchdog_policy_t p{500000, 100000};
  Bytes data(8);
  jw_encode_watchdog_policy(data.data(), data.size(), &p);
  EXPECT_EQ(Exchange(JW_CMD_CONFIGURE_WATCHDOGS, JW_CHANNEL_NONE, data).status, JW_STATUS_BUSY);
}
TEST_F(MixedDeviceTest, QueriesDoNotRefreshMovingTargetsAndDuplicatesRefreshNeitherWatchdog) {
  Velocity();
  const auto target = last_request;
  Tick(50000);
  EXPECT_EQ(Send(target).status, JW_STATUS_OK);
  EXPECT_EQ(device.watchdogs.last_target_us[0], 0);
  EXPECT_EQ(device.watchdogs.last_request_us, 0);
  EXPECT_EQ(Exchange(JW_CMD_GET_STATUS, JW_CHANNEL_NONE).status, JW_STATUS_OK);
  EXPECT_EQ(device.watchdogs.last_request_us, 50000);
  Tick(100001);
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_TRUE(StepDirDeviceRuntime()->estopped);
  EXPECT_EQ(StepDirDeviceRuntime()->state.reason, JW_REASON_DEVICE_FAULT);
}
class FractionalPositionTest : public MixedDeviceTest,
                               public ::testing::WithParamInterface<float> {};
TEST_P(FractionalPositionTest, ReachingRoundedGoalDoesNotExpireTargetWatchdog) {
  Configure();
  Policy();
  ASSERT_EQ(Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_ENABLE).status, JW_STATUS_OK);
  Bytes bytes(JW_SET_TARGET_PAYLOAD_LEN);
  jw_encode_set_target_payload(bytes.data(), bytes.size(), JW_MODE_POSITION, GetParam());
  ASSERT_EQ(Profile(JW_PROFILE_ACTUATOR, JW_ACTUATOR_SET_TARGET, bytes).status, JW_STATUS_OK);
  for (uint32_t now = 1000; now <= 40000; now += 1000) Tick(now);
  ASSERT_EQ(g_channels[0].step_dir.position_steps, std::lround(GetParam()));
  ASSERT_TRUE(g_channels[0].enabled);
  // Expire the target watchdog while remaining inside the communication budget.
  Tick(100001);
  EXPECT_TRUE(g_channels[0].enabled);
  EXPECT_FALSE(StepDirDeviceRuntime()->estopped);
}
INSTANTIATE_TEST_SUITE_P(
    RoundedGoals,
    FractionalPositionTest,
    ::testing::Values(8.888889f, -8.888889f, 8.5f, -8.5f, 8.1f, -8.1f, 0.4f, -0.4f));
TEST_F(MixedDeviceTest, CommunicationLossStopsSensingAndResetPreservesTheFaultLatch) {
  Subscribe();
  Tick(0);
  Tick(500001);
  EXPECT_TRUE(StepDirDeviceRuntime()->estopped);
  EXPECT_FALSE(device.sensor.subscription.state.active);
  session = 88;
  message = 0;
  EXPECT_EQ(Exchange(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE).status, JW_STATUS_OK);
  EXPECT_TRUE(StepDirDeviceRuntime()->estopped);
  EXPECT_TRUE(device.sensor.blocked);
  EXPECT_EQ(Exchange(JW_CMD_RECOVER, JW_CHANNEL_NONE, {0, 0}).status, JW_STATUS_OK);
  EXPECT_FALSE(device.sensor.blocked);
  EXPECT_FALSE(device.watchdogs.configured);
  EXPECT_FALSE(g_channels[0].enabled);
}
TEST_F(MixedDeviceTest, LatestSampleOverwriteAndRateLimitsAreObservable) {
  Subscribe();
  Tick(0);
  Tick(100);
  Tick(10000);
  Tick(10100);
  uint8_t frame[JW_MAX_FRAME_LEN];
  uint32_t sequence;
  const int size = JoshuaMixedDeviceSample(&device, frame, sizeof(frame), &sequence);
  ASSERT_GT(size, 0);
  jw_stream_sample_t sample{};
  ASSERT_EQ(jw_decode_stream_sample(frame, size, &sample), JW_RESULT_OK);
  EXPECT_EQ(sample.dropped_acquisitions, 1);
  EXPECT_EQ(sample.sample.metadata.sequence, 2);
  JoshuaMixedDeviceSubmitted(&device, sequence);
  EXPECT_EQ(JoshuaMixedDeviceSample(&device, frame, sizeof(frame), &sequence), 0);
}
TEST_F(MixedDeviceTest, ReservedSafetyReplyAndMotorServiceSurviveOrdinaryTxBackpressure) {
  struct LinkState {
    std::deque<Bytes> requests;
    std::vector<Bytes> sent;
    bool blocked = true;
  } link;
  frame_transport_t transport{&link,
                              [](void* p, uint8_t* bytes, size_t, size_t* count) {
                                auto* s = static_cast<LinkState*>(p);
                                *count = 0;
                                if (s->requests.empty()) return FRAME_NO_DATA;
                                auto frame = std::move(s->requests.front());
                                s->requests.pop_front();
                                std::memcpy(bytes, frame.data(), frame.size());
                                *count = frame.size();
                                return FRAME_OK;
                              },
                              [](void* p, const uint8_t* bytes, size_t count) {
                                auto* s = static_cast<LinkState*>(p);
                                if (s->blocked) return FRAME_WOULD_BLOCK;
                                s->sent.emplace_back(bytes, bytes + count);
                                return FRAME_OK;
                              }};
  jw_mixed_serial_t scheduler{};
  jw_mixed_serial_ops_t ops{&device,
                            [](void*) { StepDirDeviceService(); },
                            [](void*, const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
                              return StepDirDeviceProcess(in, n, out, cap);
                            },
                            [](void*) { StepDirDeviceFault(); },
                            JoshuaMixedDeviceSample,
                            JoshuaMixedDeviceSubmitted};
  jw_mixed_serial_init(&scheduler, transport, ops);
  auto ordinary = Frame(JW_CMD_GET_STATUS, JW_CHANNEL_NONE);
  auto stop = Frame(JW_CMD_ESTOP, JW_CHANNEL_NONE, {0, 0});
  link.requests.push_back(ordinary);
  link.requests.push_back(stop);
  jw_mixed_serial_service(&scheduler);
  EXPECT_GT(scheduler.ordinary_len, 0);
  jw_mixed_serial_service(&scheduler);
  EXPECT_GT(scheduler.safety_len, 0);
  EXPECT_TRUE(StepDirDeviceRuntime()->estopped);
  link.blocked = false;
  jw_mixed_serial_service(&scheduler);
  jw_mixed_serial_service(&scheduler);
  ASSERT_EQ(link.sent.size(), 2);
  jw_frame_t first{}, second{};
  ASSERT_EQ(jw_decode_frame(link.sent[0].data(), link.sent[0].size(), &first), JW_RESULT_OK);
  ASSERT_EQ(jw_decode_frame(link.sent[1].data(), link.sent[1].size(), &second), JW_RESULT_OK);
  EXPECT_EQ(first.cmd, JW_CMD_ESTOP);
  EXPECT_EQ(second.cmd, JW_CMD_GET_STATUS);
  EXPECT_EQ(scheduler.admission_faults, 0);
  link.blocked = true;
  link.requests.push_back(Frame(JW_CMD_GET_STATUS, JW_CHANNEL_NONE));
  jw_mixed_serial_service(&scheduler);
  const Bytes retained(scheduler.ordinary, scheduler.ordinary + scheduler.ordinary_len);
  auto unadmitted = Frame(JW_CMD_GET_STATUS, JW_CHANNEL_NONE);
  link.requests.push_back(unadmitted);
  jw_mixed_serial_service(&scheduler);
  EXPECT_EQ(scheduler.admission_faults, 1);
  EXPECT_EQ(Bytes(scheduler.ordinary, scheduler.ordinary + scheduler.ordinary_len), retained);
  const auto fresh = Send(unadmitted);
  jw_device_status_t status{};
  ASSERT_EQ(jw_decode_device_status(fresh.data, fresh.data_len, &status), JW_RESULT_OK);
  EXPECT_EQ(status.reason,
            JW_REASON_DEVICE_FAULT);  // Query was not dispatched/cached before fault.
}
}  // namespace
