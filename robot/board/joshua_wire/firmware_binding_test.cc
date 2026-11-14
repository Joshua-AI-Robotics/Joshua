// Runs real board bindings, CommFactory, serial framing and the MCU's actual
// endpoint/STEP-DIR backend together over PTYs. GPIO/clock alone are simulated.
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#include "firmware/common/joshua_peripheral_device.h"
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
  std::filesystem::path alias_directory;
  std::string alias_port;
  bool drop_target_reply = false;
  void SetUp() override {
    master = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_GE(master, 0);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    config = MakeStepDirBoardConfig(GetParam());
    config.mutable_comm()->mutable_serial_config()->set_port(ptsname(master));
    char directory[] = "/tmp/joshua-wire-alias-XXXXXX";
    ASSERT_NE(mkdtemp(directory), nullptr);
    alias_directory = directory;
    alias_port = (alias_directory / "serial").string();
    std::filesystem::create_symlink(config.comm().serial_config().port(), alias_port);
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
    if (!alias_directory.empty()) std::filesystem::remove_all(alias_directory);
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

TEST_P(FirmwareBindingTest, ProfileWiringUsesExistingFirmwarePayloadAndSharedCache) {
  if (GetParam() == AM243) GTEST_SKIP() << "No AM243 connector profile in this increment";
  config.mutable_pin_profile()->set_id(
      GetParam() == TEENSY41 ? "pjrc/teensy41" : "espressif/esp32-devkitc-v4-wroom32");
  config.mutable_pin_profile()->set_version(1);
  auto* c = config.mutable_channels(0);
  c->mutable_step_dir()->clear_step_pin();
  c->mutable_step_dir()->clear_dir_pin();
  c->mutable_step_dir()->clear_enable_pin();
  auto* w = c->mutable_step_dir_wiring();
  const uint32_t first = GetParam() == TEENSY41 ? 0 : 25;
  const uint32_t direction = GetParam() == TEENSY41 ? 2 : 26;
  const uint32_t enable = GetParam() == TEENSY41 ? 3 : 27;
  w->mutable_step()->set_firmware_pin(first);
  w->mutable_direction()->set_firmware_pin(direction);
  w->mutable_enable()->set_firmware_pin(enable);
  auto board = BoardFactory::GetOrCreate(config);
  ASSERT_TRUE(board.ok()) << board.status();
  {
    std::lock_guard lock(firmware_mutex);
    EXPECT_EQ(g_channels[0].step_dir.config.step_pin, first);
    EXPECT_EQ(g_channels[0].step_dir.config.dir_pin, direction);
    EXPECT_EQ(g_channels[0].step_dir.config.enable_pin, enable);
    EXPECT_FALSE(g_channels[0].enabled);
  }
  auto native = config;
  native.mutable_channels(0)->clear_step_dir_wiring();
  native.mutable_channels(0)->mutable_step_dir()->set_step_pin(first);
  native.mutable_channels(0)->mutable_step_dir()->set_dir_pin(direction);
  native.mutable_channels(0)->mutable_step_dir()->set_enable_pin(enable);
  auto shared = BoardFactory::GetOrCreate(native);
  ASSERT_TRUE(shared.ok()) << shared.status();
  EXPECT_EQ(board->get(), shared->get());
}

struct PeripheralFake {
  jw_peripheral_config_t config[8]{};
  bool configured[8]{};
  bool enabled[8]{};
  uint8_t value[8]{};
  std::vector<uint8_t> uart;
  unsigned transfers = 0;
  bool fail = false;
  unsigned board = 1;
  jw_peripheral_ops_t Ops() {
    return {this,
            [](void* p, const jw_peripheral_config_t* c) {
              return jw_peripheral_route_valid(static_cast<PeripheralFake*>(p)->board, c)
                         ? JW_STATUS_OK
                         : JW_STATUS_INVALID_PAYLOAD;
            },
            [](void* p, unsigned i, const jw_peripheral_config_t* c) {
              auto& f = *static_cast<PeripheralFake*>(p);
              f.config[i] = *c;
              f.configured[i] = true;
              return JW_STATUS_OK;
            },
            [](void* p,
               unsigned i,
               uint8_t op,
               const uint8_t* q,
               size_t n,
               uint8_t* out,
               size_t* size) {
              auto& f = *static_cast<PeripheralFake*>(p);
              *size = 0;
              if (op == JW_PERIPHERAL_TRANSFER) {
                ++f.transfers;
                if (f.fail) return JW_STATUS_ERROR;
                out[0] = q[2];
                *size = 1 + q[2];
                for (unsigned j = 0; j < q[2]; ++j)
                  out[j + 1] = q[0] == JW_PERIPHERAL_SPI ? q[j + 3] : 42;
                if (q[0] == JW_PERIPHERAL_UART && q[1]) f.uart.assign(q + 3, q + n);
              } else if (op == JW_PERIPHERAL_READ) {
                out[0] = f.value[i];
                *size = 1;
              } else if (op == JW_PERIPHERAL_WRITE) {
                f.value[i] = q[0];
                std::copy(q, q + n, out);
                *size = n;
              } else if (op == JW_PERIPHERAL_ENABLE || op == JW_PERIPHERAL_DISABLE)
                f.enabled[i] = op == JW_PERIPHERAL_ENABLE;
              return JW_STATUS_OK;
            },
            [](void* p, unsigned i) {
              auto& f = *static_cast<PeripheralFake*>(p);
              f.configured[i] = false;
              f.enabled[i] = false;
              return JW_STATUS_OK;
            },
            [](void*) {
              for (uint8_t i = 0; i < g_num_channels; ++i)
                if (g_channels[i].enabled) return 0;
              return 1;
            }};
  }
};
void AddPeripherals(Board* b) {
  const bool teensy = b->board_type() == TEENSY41;
  b->mutable_pin_profile()->set_id(teensy ? "pjrc/teensy41" : "espressif/esp32-devkitc-v4-wroom32");
  b->mutable_pin_profile()->set_version(1);
  auto* motor = b->mutable_channels(0)->mutable_step_dir();
  motor->set_step_pin(teensy ? 2 : 25);
  motor->set_dir_pin(teensy ? 3 : 26);
  motor->set_enable_pin(teensy ? 4 : 27);
  auto add = [&](const char* name, unsigned id) {
    auto* p = b->add_peripherals();
    p->set_name(name);
    p->set_purpose("test peripheral");
    p->set_function_id(id);
    return p;
  };
  auto* p = add("pwm", 192);
  p->mutable_pwm()->mutable_pin()->set_firmware_pin(teensy ? 6 : 4);
  p->mutable_pwm()->set_frequency_hz(1000);
  p->mutable_pwm()->set_initial_duty_cycle(0.25);
  auto* s = add("spi", 193)->mutable_spi();
  s->mutable_clock()->set_firmware_pin(teensy ? 13 : 18);
  s->mutable_mosi()->set_firmware_pin(teensy ? 11 : 23);
  s->mutable_miso()->set_firmware_pin(teensy ? 12 : 19);
  s->mutable_chip_select()->set_firmware_pin(teensy ? 10 : 13);
  s->mutable_settings()->set_speed_hz(1000000);
  auto* i = add("i2c", 194)->mutable_i2c();
  i->mutable_sda()->set_firmware_pin(teensy ? 18 : 21);
  i->mutable_scl()->set_firmware_pin(teensy ? 19 : 22);
  i->mutable_settings()->set_address(0x48);
  i->mutable_settings()->set_frequency_hz(100000);
  i->mutable_settings()->set_timeout_ms(teensy ? 50 : 5);
  auto* u = add("uart", 195)->mutable_uart();
  u->set_bus_index(1);
  u->mutable_settings()->set_baudrate(115200);
  u->mutable_tx()->set_firmware_pin(teensy ? 1 : 17);
  u->mutable_rx()->set_firmware_pin(teensy ? 0 : 16);
  auto* d = add("digital", 196)->mutable_digital();
  d->mutable_pin()->set_firmware_pin(teensy ? 5 : 32);
  d->set_direction(DIGITAL_OUTPUT);
  d->set_initial_value(false);
  d->set_shutdown_value(false);
  b->mutable_comm()->mutable_serial_config()->set_exchange_timeout_ms(500);
}
TEST_P(FirmwareBindingTest, PeripheralCapabilitiesShareSessionReplayAndTerminalLifetime) {
  if (GetParam() == AM243) GTEST_SKIP();
  PeripheralFake fake;
  fake.board = GetParam() == TEENSY41 ? 1 : 2;
  {
    std::lock_guard lock(firmware_mutex);
    ASSERT_TRUE(JoshuaPeripheralDeviceInit(fake.Ops()));
  }
  AddPeripherals(&config);
  auto board = BoardFactory::GetOrCreate(config);
  ASSERT_TRUE(board.ok()) << board.status();
  auto pwm = (*board)->OpenPwm("pwm");
  auto spi = (*board)->OpenSpi("spi");
  auto i2c = (*board)->OpenI2c("i2c");
  auto uart = (*board)->OpenUart("uart");
  auto digital = (*board)->OpenDigitalPin("digital");
  ASSERT_TRUE(pwm.ok());
  ASSERT_TRUE(spi.ok());
  ASSERT_TRUE(i2c.ok());
  ASSERT_TRUE(uart.ok());
  ASSERT_TRUE(digital.ok());
  EXPECT_FALSE((*board)->OpenPwm("spi").ok());
  ASSERT_TRUE((*pwm)->SetDutyCycle(.5).ok());
  ASSERT_TRUE((*pwm)->Enable().ok());
  {
    std::lock_guard lock(firmware_mutex);
    drop_target_reply = true;
  }
  EXPECT_EQ((*spi)->Transfer({1, 2, 3}).value(), (Bytes{1, 2, 3}));
  {
    std::lock_guard lock(firmware_mutex);
    EXPECT_EQ(fake.transfers, 1u);
  }
  EXPECT_EQ((*i2c)->Transfer({0}, 2).value(), (Bytes{42, 42}));
  EXPECT_TRUE((*uart)->Write({7, 8}).ok());
  EXPECT_EQ((*uart)->Read(2).value(), (Bytes{42, 42}));
  EXPECT_TRUE((*digital)->Write(true).ok());
  EXPECT_TRUE((*digital)->Read().value());
  auto channel = (*board)->OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  ASSERT_TRUE((*channel)->Enable().ok());
  EXPECT_EQ((*spi)->Transfer({1}).status().code(), absl::StatusCode::kResourceExhausted);
  EXPECT_TRUE((*pwm)->Disable().ok());
  ASSERT_TRUE((*channel)->Disable().ok());
  ASSERT_TRUE((*board)->Teardown().ok());
  EXPECT_FALSE((*pwm)->Enable().ok());
  EXPECT_FALSE((*spi)->Transfer({1}).ok());
  EXPECT_FALSE((*i2c)->Transfer({}, 1).ok());
  EXPECT_FALSE((*uart)->Open().ok());
  EXPECT_FALSE((*digital)->Write(true).ok());
  {
    std::lock_guard lock(firmware_mutex);
    for (bool active : fake.configured) EXPECT_FALSE(active);
  }
}
TEST_P(FirmwareBindingTest, BusFailureStopsAllOutputsAndLatchesUnknownOutcome) {
  if (GetParam() == AM243) GTEST_SKIP();
  PeripheralFake fake;
  fake.board = GetParam() == TEENSY41 ? 1 : 2;
  {
    std::lock_guard lock(firmware_mutex);
    ASSERT_TRUE(JoshuaPeripheralDeviceInit(fake.Ops()));
  }
  AddPeripherals(&config);
  auto board = BoardFactory::GetOrCreate(config);
  ASSERT_TRUE(board.ok()) << board.status();
  auto pwm = (*board)->OpenPwm("pwm");
  auto spi = (*board)->OpenSpi("spi");
  ASSERT_TRUE(pwm.ok());
  ASSERT_TRUE(spi.ok());
  ASSERT_TRUE((*pwm)->Enable().ok());
  {
    std::lock_guard lock(firmware_mutex);
    fake.fail = true;
  }
  EXPECT_EQ((*spi)->Transfer({1}).status().code(), absl::StatusCode::kUnknown);
  EXPECT_FALSE((*pwm)->Enable().ok());
  {
    std::lock_guard lock(firmware_mutex);
    EXPECT_TRUE(StepDirDeviceRuntime()->estopped);
    for (bool active : fake.configured) EXPECT_FALSE(active);
    EXPECT_EQ(fake.transfers, 1u);
  }
  (*board)->Teardown().IgnoreError();
}

TEST_P(FirmwareBindingTest, SerialAliasIsRejectedBeforeResettingTheExistingSession) {
  auto first = BoardFactory::GetOrCreate(config);
  ASSERT_TRUE(first.ok()) << first.status();
  auto retained = (*first)->OpenChannel(0);
  ASSERT_TRUE(retained.ok());
  ASSERT_TRUE((*retained)->Enable().ok());
  uint32_t epoch;
  {
    std::lock_guard lock(firmware_mutex);
    epoch = g_functions[0].state.epoch;
  }
  auto alias = config;
  alias.set_name("same_port_second_board");
  auto second = BoardFactory::GetOrCreate(alias);
  EXPECT_EQ(second.status().code(), absl::StatusCode::kAlreadyExists);
  {
    std::lock_guard lock(firmware_mutex);
    EXPECT_TRUE(g_channels[0].enabled);
    EXPECT_TRUE(g_channels[0].configured);
    EXPECT_EQ(g_functions[0].state.epoch, epoch);
  }
  ASSERT_TRUE((*retained)->SetTarget(TargetMode::kPosition, 10).ok());
  auto shared = BoardFactory::GetOrCreate(config);
  ASSERT_TRUE(shared.ok());
  EXPECT_EQ(shared->get(), first->get());
  ASSERT_TRUE((*first)->Teardown().ok());
  EXPECT_EQ(BoardFactory::GetOrCreate(alias).status().code(), absl::StatusCode::kAlreadyExists);
  BoardFactory::ResetForTesting();
  auto reopened = BoardFactory::GetOrCreate(alias);
  ASSERT_TRUE(reopened.ok()) << reopened.status();
}

TEST_P(FirmwareBindingTest, SymlinkCannotResetAnAlreadyOwnedSerialSession) {
  auto first = BoardFactory::GetOrCreate(config);
  ASSERT_TRUE(first.ok()) << first.status();
  auto channel = (*first)->OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  ASSERT_TRUE((*channel)->Enable().ok());
  uint32_t epoch;
  {
    std::lock_guard lock(firmware_mutex);
    epoch = g_functions[0].state.epoch;
  }

  auto alias = config;
  alias.mutable_comm()->mutable_serial_config()->set_port(alias_port);
  // The same board declaration through an alias must reuse its active session.
  auto shared = BoardFactory::GetOrCreate(alias);
  EXPECT_TRUE(shared.ok()) << shared.status();
  if (shared.ok()) EXPECT_EQ(shared->get(), first->get());
  alias.set_name("second_board_using_symlink");
  EXPECT_EQ(BoardFactory::GetOrCreate(alias).status().code(), absl::StatusCode::kAlreadyExists);
  {
    std::lock_guard lock(firmware_mutex);
    EXPECT_TRUE(g_channels[0].enabled);
    EXPECT_TRUE(g_channels[0].configured);
    EXPECT_EQ(g_functions[0].state.epoch, epoch);
  }
  EXPECT_TRUE((*channel)->SetTarget(TargetMode::kPosition, 10).ok());
}

TEST_P(FirmwareBindingTest, FailedInitializationDoesNotReserveSerialEndpoint) {
  auto invalid = config;
  invalid.mutable_channels(0)->set_index(200);  // Not exposed by the actual firmware.
  EXPECT_FALSE(BoardFactory::GetOrCreate(invalid).ok());
  auto retry = config;
  retry.set_name("retry_after_failed_initialization");
  auto board = BoardFactory::GetOrCreate(retry);
  ASSERT_TRUE(board.ok()) << board.status();
  auto channel = (*board)->OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  EXPECT_TRUE((*channel)->Enable().ok());
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
