// Runs real board bindings, CommFactory, serial framing and the MCU's actual
// endpoint/STEP-DIR backend together over PTYs. GPIO/clock alone are simulated.
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <vector>

#include "firmware/common/joshua_stepdir_device.h"
#include "firmware/common/joshua_wire_stream.h"
#include "gtest/gtest.h"
#include "robot/board/factory/board_factory.h"
#include "robot/board/joshua_wire/testing/board_config.h"
#include "robot/comm/factory/comm_factory.h"

ChannelState g_channels[2];
jw_runtime_function_t g_functions[2];
jw_resource_claim_t g_stepdir_resources[6];
const uint8_t g_num_channels = 2;
namespace {
uint32_t now_us;
std::array<uint8_t, 256> pins;
uint32_t gpio_writes;
bool ValidPin(uint8_t pin) {
  return pin < 40;
}
}  // namespace
unsigned long micros() {
  return now_us;
}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t pin, uint8_t value) {
  pins[pin] = value;
  ++gpio_writes;
}

namespace robot::board {
namespace {
using Bytes = std::vector<uint8_t>;

class FirmwareBindingTest : public ::testing::TestWithParam<BoardType> {
 protected:
  std::mutex firmware_mutex;
  std::atomic<bool> stop{false};
  int master = -1;
  std::thread device;
  Board config;
  bool drop_target_reply = false;
  void SetUp() override {
    master = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_GE(master, 0);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    config = MakeStepDirBoardConfig(GetParam());
    config.mutable_comm()->mutable_serial_config()->set_port(ptsname(master));
    config.mutable_comm()->mutable_serial_config()->set_exchange_timeout_ms(50);
    now_us = 1000;
    pins.fill(0);
    gpio_writes = 0;
    StepDirDeviceInit(123456, "native-stepdir", ValidPin);
    device = std::thread([this] {
      jw_stream_decoder_t stream{};
      while (!stop) {
        pollfd descriptor{master, POLLIN, 0};
        const int ready = poll(&descriptor, 1, 1);
        uint8_t bytes[JW_MAX_FRAME_LEN];
        const ssize_t count =
            ready > 0 && (descriptor.revents & POLLIN) ? read(master, bytes, sizeof(bytes)) : 0;
        std::lock_guard lock(firmware_mutex);
        now_us += 1000;
        StepDirDeviceService();
        for (ssize_t i = 0; i < count; ++i) {
          const int size = jw_stream_feed(&stream, bytes[i], now_us / 1000);
          if (size <= 0) continue;
          uint8_t response[JW_MAX_FRAME_LEN];
          const int result = StepDirDeviceProcess(stream.bytes, size, response, sizeof(response));
          jw_frame_t frame;
          if (drop_target_reply && jw_decode_frame(stream.bytes, size, &frame) == JW_RESULT_OK &&
              frame.cmd == JW_CMD_PROFILE && frame.payload_len >= JW_PROFILE_HEADER_LEN &&
              frame.payload[JW_PROFILE_HEADER_LEN - 1] == JW_ACTUATOR_SET_TARGET) {
            drop_target_reply = false;
            continue;
          }
          if (result > 0) (void)write(master, response, result);
        }
      }
    });
  }
  void TearDown() override {
    BoardFactory::ResetForTesting();
    robot::comm::CommFactory::ResetSerialTransportCacheForTesting();
    stop = true;
    if (device.joinable()) device.join();
    if (master >= 0) close(master);
  }
};

TEST_P(FirmwareBindingTest, FactoryDiscoversCurrentFirmwareAndDrivesConfiguredBackend) {
  auto board = BoardFactory::GetOrCreate(config);
  ASSERT_TRUE(board.ok()) << board.status();
  {
    std::lock_guard lock(firmware_mutex);
    EXPECT_TRUE(g_channels[0].configured);
    EXPECT_FALSE(g_channels[0].enabled);
    EXPECT_FALSE(g_channels[1].configured);
  }
  auto channel = (*board)->OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  ASSERT_TRUE((*channel)->Enable().ok());
  ASSERT_TRUE((*channel)->SetTarget(TargetMode::kPosition, 3).ok());
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
  for (;;) {
    {
      std::lock_guard lock(firmware_mutex);
      if (g_channels[0].step_dir.position_steps == 3) break;
    }
    ASSERT_LT(std::chrono::steady_clock::now(), deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  auto feedback = (*channel)->ReadFeedback();
  ASSERT_TRUE(feedback.ok()) << feedback.status();
  EXPECT_EQ(feedback->position, 3);
  ASSERT_TRUE((*board)->Teardown().ok());
  {
    std::lock_guard lock(firmware_mutex);
    EXPECT_FALSE(g_channels[0].enabled);
    EXPECT_EQ(pins[4], 1);  // Active-low enable deasserted synchronously.
    EXPECT_EQ(pins[2], 0);
  }
}

TEST_P(FirmwareBindingTest, NormalShutdownAndReopenKeepOutputsDisabledWithoutEstop) {
  for (int restart = 0; restart < 2; ++restart) {
    auto board = BoardFactory::GetOrCreate(config);
    ASSERT_TRUE(board.ok()) << board.status();
    auto channel = (*board)->OpenChannel(0);
    ASSERT_TRUE(channel.ok());
    {
      std::lock_guard lock(firmware_mutex);
      EXPECT_TRUE(g_channels[0].configured);
      EXPECT_FALSE(g_channels[0].enabled);
    }
    ASSERT_TRUE((*channel)->Enable().ok());
    ASSERT_TRUE((*channel)->SetTarget(TargetMode::kVelocity, 100).ok());
    ASSERT_TRUE((*board)->Teardown().ok());
    {
      std::lock_guard lock(firmware_mutex);
      EXPECT_FALSE(g_channels[0].configured);
      EXPECT_FALSE(g_channels[0].enabled);
      EXPECT_EQ(g_functions[0].state.recovery & JW_RECOVERY_CLEAR_ESTOP, 0);
      EXPECT_EQ(pins[4], 1);
      EXPECT_EQ(pins[2], 0);
    }
    EXPECT_EQ((*channel)->Enable().code(), absl::StatusCode::kFailedPrecondition);
    BoardFactory::ResetForTesting();
    robot::comm::CommFactory::ResetSerialTransportCacheForTesting();
  }
}

TEST_P(FirmwareBindingTest, LostReplyReplaysWithoutRepeatingBackendMutation) {
  auto board = BoardFactory::GetOrCreate(config);
  ASSERT_TRUE(board.ok()) << board.status();
  auto channel = (*board)->OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  ASSERT_TRUE((*channel)->Enable().ok());
  uint16_t epoch;
  {
    std::lock_guard lock(firmware_mutex);
    epoch = g_functions[0].state.epoch;
    drop_target_reply = true;
  }
  const auto status = (*channel)->SetTarget(TargetMode::kPosition, 5);
  ASSERT_TRUE(status.ok()) << status;
  std::lock_guard lock(firmware_mutex);
  EXPECT_FALSE(drop_target_reply);
  EXPECT_EQ(g_functions[0].state.epoch, epoch + 1);
}

TEST_P(FirmwareBindingTest, PinConflictRejectsSecondFunctionWithoutChangingFirst) {
  auto* second = config.add_channels();
  *second = config.channels(0);
  second->set_index(1);
  auto board = BoardFactory::GetOrCreate(config);
  EXPECT_FALSE(board.ok());
  std::lock_guard lock(firmware_mutex);
  EXPECT_FALSE(g_channels[1].configured);
  EXPECT_FALSE(g_channels[0].enabled);
}

INSTANTIATE_TEST_SUITE_P(CurrentMcuBindings,
                         FirmwareBindingTest,
                         ::testing::Values(AM243, TEENSY41, ESP32));

class FirmwareProtocolTest : public ::testing::Test {
 protected:
  uint32_t message = 0;
  uint32_t session = 88;
  void SetUp() override {
    pins.fill(0);
    now_us = 1000;
    StepDirDeviceInit(77, "native", ValidPin);
    EXPECT_EQ(Exchange(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE, {}).status, JW_STATUS_OK);
  }
  jw_reply_t Exchange(uint8_t command, uint8_t function, const Bytes& payload) {
    uint8_t request[JW_MAX_FRAME_LEN];
    int size = jw_encode_frame(request,
                               sizeof(request),
                               session,
                               ++message,
                               command,
                               function,
                               payload.data(),
                               payload.size());
    int result = StepDirDeviceProcess(request, size, response.data(), response.size());
    EXPECT_GT(result, 0);
    jw_frame_t frame;
    EXPECT_EQ(jw_decode_frame(response.data(), result, &frame), JW_RESULT_OK);
    jw_reply_t reply{};
    EXPECT_EQ(jw_decode_reply_payload(frame.payload, frame.payload_len, &reply), JW_RESULT_OK);
    return reply;
  }
  jw_reply_t Profile(uint8_t operation, const Bytes& data = {}, uint8_t function = 0) {
    Bytes bytes(JW_MAX_PAYLOAD_LEN);
    jw_profile_request_t profile{JW_PROFILE_ACTUATOR,
                                 JW_PROFILE_VERSION_MAJOR,
                                 JW_PROFILE_VERSION_MINOR,
                                 operation,
                                 data.data(),
                                 data.size()};
    bytes.resize(jw_encode_profile_request(bytes.data(), bytes.size(), &profile));
    return Exchange(JW_CMD_PROFILE, function, bytes);
  }
  void Configure() {
    jw_configure_step_dir_t config{4000, 0, 1, 2, 3, 4, 20};
    Bytes bytes(JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN);
    jw_encode_configure_step_dir_payload(bytes.data(), bytes.size(), &config);
    ASSERT_EQ(Profile(JW_ACTUATOR_CONFIGURE, bytes).status, JW_STATUS_OK);
  }
  std::array<uint8_t, JW_MAX_FRAME_LEN> response{};
};

TEST_F(FirmwareProtocolTest, ResetAndRecoverDisableHardwareAndPreserveEstopUntilRecovery) {
  Configure();
  ASSERT_EQ(Profile(JW_ACTUATOR_ENABLE).status, JW_STATUS_OK);
  EXPECT_EQ(pins[4], 0);
  EXPECT_EQ(Exchange(JW_CMD_ESTOP, JW_CHANNEL_NONE, {0, 0}).status, JW_STATUS_OK);
  EXPECT_EQ(pins[4], 1);
  session = 99;
  EXPECT_EQ(Exchange(JW_CMD_RESET_SESSION, JW_CHANNEL_NONE, {}).status, JW_STATUS_OK);
  EXPECT_EQ(Profile(JW_ACTUATOR_ENABLE).status, JW_STATUS_UNAVAILABLE);
  EXPECT_EQ(Exchange(JW_CMD_RECOVER, JW_CHANNEL_NONE, {0, 0}).status, JW_STATUS_OK);
  EXPECT_FALSE(g_channels[0].configured);
  EXPECT_FALSE(g_channels[0].enabled);
  Configure();
  EXPECT_FALSE(g_channels[0].enabled);
}

TEST_F(FirmwareProtocolTest, RetiredFramesAreDiscardedWithoutSideEffects) {
  const Bytes retired{0xa5, 0x03, 0x01, 0x05, 0x00, 0x59, 0x04};
  const auto writes = gpio_writes;
  EXPECT_EQ(StepDirDeviceProcess(retired.data(), retired.size(), response.data(), response.size()),
            0);
  EXPECT_EQ(gpio_writes, writes);
  jw_stream_decoder_t stream{};
  for (auto byte : retired) EXPECT_LE(jw_stream_feed(&stream, byte, 0), 0);
}

TEST_F(FirmwareProtocolTest, ResourceConflictHasNoPartialHardwareOrConfigurationEffects) {
  Configure();
  const auto writes = gpio_writes;
  jw_configure_step_dir_t config{4000, 0, 1, 2, 3, 4, 20};
  Bytes bytes(JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN);
  jw_encode_configure_step_dir_payload(bytes.data(), bytes.size(), &config);
  EXPECT_EQ(Profile(JW_ACTUATOR_CONFIGURE, bytes, 1).status, JW_STATUS_RESOURCE_CONFLICT);
  EXPECT_EQ(gpio_writes, writes);
  EXPECT_TRUE(g_channels[0].configured);
  EXPECT_FALSE(g_channels[1].configured);
  EXPECT_EQ(g_stepdir_resources[0].owner, 0);
}

TEST_F(FirmwareProtocolTest, PartialSerialInputExpiresAndCurrentFrameCanFollow) {
  jw_stream_decoder_t stream{};
  EXPECT_EQ(jw_stream_feed(&stream, JW_SYNC_BYTE, 0), 0);
  EXPECT_EQ(jw_stream_feed(&stream, 30, 1), 0);
  uint8_t frame[JW_MAX_FRAME_LEN];
  int length = jw_encode_frame(
      frame, sizeof(frame), session, ++message, JW_CMD_IDENTIFY, JW_CHANNEL_NONE, nullptr, 0);
  int result = 0;
  for (int i = 0; i < length; ++i) result = jw_stream_feed(&stream, frame[i], 30 + i);
  EXPECT_EQ(result, length);
}
}  // namespace
}  // namespace robot::board
