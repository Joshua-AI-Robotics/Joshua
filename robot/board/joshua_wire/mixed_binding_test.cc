// Production board/perception + MCU_BUS over a PTY and the actual mixed loop.
// Only GPIO, ADC and time are native hooks; no device is opened or flashed.
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <sstream>
#include <thread>

#include "config/config_utils.h"
#include "config/validation.h"
#include "firmware/common/joshua_mixed_device.h"
#include "firmware/common/joshua_mixed_serial.h"
#include "firmware/common/joshua_wire_stream.h"
#include "gtest/gtest.h"
#include "rclcpp/version.h"
#include "robot/board/factory/board_factory.h"
#include "robot/board/joshua_wire/serial_validation.h"
#include "robot/comm/factory/comm_factory.h"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/mcu_bus.h"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/float32.hpp"
#include "utils/joshua_runtime.h"
extern char** environ;

ChannelState g_channels[1];
jw_runtime_function_t g_functions[1];
jw_resource_claim_t g_stepdir_resources[3];
const uint8_t g_num_channels = 1;
namespace {
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
std::array<uint8_t, 256> pins;
Clock::time_point origin;
}  // namespace
unsigned long micros() {
  return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - origin).count();
}
void pinMode(uint8_t, uint8_t) {}
void digitalWrite(uint8_t p, uint8_t v) {
  pins[p] = v;
}
namespace {
class MixedBindingTest : public ::testing::Test {
 protected:
  config::Config config;
  int master = -1;
  std::mutex firmware_mutex;
  std::thread device;
  std::atomic<bool> stopping{false}, drop_targets{false}, drop_all{false}, adc_ready{true};
  std::atomic<int> targets{0}, estops{0};
  JoshuaMixedDevice mixed{};
  jw_mixed_serial_t scheduler{};
  jw_stream_decoder_t decoder{};
  std::deque<uint8_t> rx;
  void SetUp() override {
    // Each fixture represents a new runtime. Expected faults in another test
    // must not cancel this fixture's independent serial recovery commands.
    ASSERT_EQ(joshua::RunGuarded("mixed binding test", [] { return 0; }), 0);
    ASSERT_NE(getenv("TEST_TMPDIR"), nullptr);
    ASSERT_EQ(setenv("ROS_LOG_DIR", getenv("TEST_TMPDIR"), 1), 0);
    ASSERT_EQ(setenv("ROS_DOMAIN_ID", RCLCPP_VERSION_MAJOR >= 20 ? "84" : "83", 1), 0);
    ASSERT_EQ(setenv("ROS_LOCALHOST_ONLY", "1", 1), 0);
    if (!rclcpp::ok()) rclcpp::init(0, nullptr);
    auto loaded =
        config::config_util::LoadConfig("config/config_preset/example/teensy_mixed_scalar.pbtxt");
    ASSERT_TRUE(loaded.ok()) << loaded.status();
    config = *loaded;
    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    ASSERT_GE(master, 0);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    config.mutable_robot()->mutable_boards(0)->mutable_comm()->mutable_serial_config()->set_port(
        ptsname(master));
    ASSERT_TRUE(config::ValidateConfig(config).ok());
    origin = Clock::now();
    Boot(1234);
    frame_transport_t transport{
        this,
        [](void* ctx, uint8_t* out, size_t capacity, size_t* length) {
          auto* s = static_cast<MixedBindingTest*>(ctx);
          *length = 0;
          uint8_t bytes[96];
          auto count = read(s->master, bytes, sizeof(bytes));
          for (ssize_t i = 0; i < count; ++i) s->rx.push_back(bytes[i]);
          while (!s->rx.empty()) {
            auto byte = s->rx.front();
            s->rx.pop_front();
            const int size = jw_stream_feed(&s->decoder, byte, micros() / 1000);
            if (size <= 0) continue;
            if (size_t(size) > capacity) return FRAME_ERROR;
            memcpy(out, s->decoder.bytes, size);
            *length = size;
            return FRAME_OK;
          }
          return FRAME_NO_DATA;
        },
        [](void* ctx, const uint8_t* bytes, size_t size) {
          auto* s = static_cast<MixedBindingTest*>(ctx);
          jw_frame_t frame{};
          if (s->drop_all) return FRAME_OK;
          if (jw_decode_frame(bytes, size, &frame) == JW_RESULT_OK && frame.cmd == JW_CMD_PROFILE &&
              s->drop_targets && frame.channel == 0) {
            // Only target replies are dropped, not status/sensor traffic.
            // Marked by dispatch below because responses carry no operation code.
            if (s->last_target) return FRAME_OK;
          }
          auto n = write(s->master, bytes, size);
          return n == ssize_t(size) ? FRAME_OK : (n < 0 ? FRAME_WOULD_BLOCK : FRAME_ERROR);
        }};
    jw_mixed_serial_ops_t ops{
        this,
        [](void*) { StepDirDeviceService(); },
        [](void* ctx, const uint8_t* bytes, size_t size, uint8_t* out, size_t capacity) {
          auto* s = static_cast<MixedBindingTest*>(ctx);
          jw_frame_t frame{};
          jw_profile_request_t p{};
          s->last_target = false;
          const bool decoded = jw_decode_frame(bytes, size, &frame) == JW_RESULT_OK;
          if (decoded && frame.cmd == JW_CMD_ESTOP) ++s->estops;
          if (decoded && frame.cmd == JW_CMD_PROFILE &&
              jw_decode_profile_request(frame.payload, frame.payload_len, &p) == JW_RESULT_OK &&
              p.profile_id == JW_PROFILE_ACTUATOR && p.operation == JW_ACTUATOR_SET_TARGET) {
            s->last_target = true;
            ++s->targets;
          }
          return StepDirDeviceProcess(bytes, size, out, capacity);
        },
        [](void*) { StepDirDeviceFault(); },
        [](void* ctx, uint8_t* out, size_t cap, uint32_t* seq) {
          return JoshuaMixedDeviceSample(
              &static_cast<MixedBindingTest*>(ctx)->mixed, out, cap, seq);
        },
        [](void* ctx, uint32_t seq) {
          JoshuaMixedDeviceSubmitted(&static_cast<MixedBindingTest*>(ctx)->mixed, seq);
        }};
    jw_mixed_serial_init(&scheduler, transport, ops);
    device = std::thread([this] {
      while (!stopping) {
        {
          std::lock_guard lock(firmware_mutex);
          jw_mixed_serial_service(&scheduler);
        }
        std::this_thread::sleep_for(100us);
      }
    });
  }
  bool last_target = false;
  void Boot(uint32_t boot) {
    StepDirDeviceInit(boot, "mixed-native-reference", [](uint8_t p) { return p < 40; });
    ASSERT_TRUE(JoshuaMixedDeviceInit(&mixed,
                                      14,
                                      {this,
                                       [](void*) { return 1; },
                                       [](void* ctx, float* value) {
                                         *value = 512;
                                         return static_cast<MixedBindingTest*>(ctx)->adc_ready ? 1
                                                                                               : 0;
                                       },
                                       [](void*) {}}));
  }
  void TearDown() override {
    robot::board::BoardFactory::ResetForTesting();
    robot::comm::CommFactory::ResetSerialTransportCacheForTesting();
    stopping = true;
    if (device.joinable()) device.join();
    if (master >= 0) close(master);
    rclcpp::shutdown();
  }
  template <class F>
  bool Await(F f, std::chrono::milliseconds timeout = 1500ms) {
    const auto end = Clock::now() + timeout;
    while (Clock::now() < end) {
      if (f()) return true;
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }
  bool Enabled() {
    std::lock_guard lock(firmware_mutex);
    return g_channels[0].enabled;
  }
};

TEST_F(MixedBindingTest, ProductionFactoriesShareSessionAndPreserveObservationMetadata) {
  auto board = robot::board::BoardFactory::GetOrCreate(config.robot().boards(0));
  ASSERT_TRUE(board.ok()) << board.status();
  auto sensor = robot::perception::PerceptionFactory::CreatePerception(
      config.robot().perceptions().single_perceptions(0), config.robot().boards());
  ASSERT_TRUE(sensor.ok()) << sensor.status();
  robot::perception::PerceptionPacket packet;
  ASSERT_TRUE(Await([&] {
    auto p = (*sensor)->GetData();
    if (!p.ok()) return false;
    packet = *p;
    return packet.scalar().usable();
  }));
  EXPECT_EQ(packet.scalar().value(), 512);
  EXPECT_EQ(packet.scalar().unit(), 65535);
  EXPECT_EQ(packet.scalar().boot_id(), 1234);
  EXPECT_GT(packet.scalar().host_receipt_steady_ns(), 0);
  EXPECT_EQ(packet.timestamp(), 0);
  auto channel = (*board)->OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  EXPECT_FALSE(Enabled());
  ASSERT_TRUE((*channel)->Enable().ok());
  ASSERT_TRUE((*channel)->SetTarget(robot::board::TargetMode::kPosition, 3).ok());
  std::this_thread::sleep_for(
      1100ms);  // Beyond communication WD, heartbeat keeps holding output alive.
  EXPECT_TRUE(Enabled());
  ASSERT_TRUE((*sensor)->GetData().ok());
  ASSERT_TRUE((*board)->Teardown().ok());
  EXPECT_FALSE(Enabled());
  EXPECT_FALSE((*sensor)->GetData().ok());
  EXPECT_FALSE((*channel)->Enable().ok());
}

TEST_F(MixedBindingTest, HeartbeatsNeverRefreshMovingTargetsAndFailureInvalidatesStreams) {
  auto board = robot::board::BoardFactory::GetOrCreate(config.robot().boards(0));
  ASSERT_TRUE(board.ok()) << board.status();
  auto sensor = (*board)->SubscribeScalar(128, 10000);
  ASSERT_TRUE(sensor.ok()) << sensor.status();
  auto channel = (*board)->OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  ASSERT_TRUE((*channel)->Enable().ok());
  ASSERT_TRUE((*channel)->SetTarget(robot::board::TargetMode::kVelocity, 100).ok());
  ASSERT_TRUE(Await([&] { return !Enabled(); }));
  ASSERT_TRUE(Await([&] { return !(*sensor)->Latest().ok(); }));
  EXPECT_FALSE((*channel)->Enable().ok());
  std::lock_guard lock(firmware_mutex);
  EXPECT_TRUE(StepDirDeviceRuntime()->estopped);
  EXPECT_EQ(pins[4], 1);
}

TEST_F(MixedBindingTest, ReservedStopRetiresLostTargetReplyWithoutReplayingOrEnabling) {
  auto board = robot::board::BoardFactory::GetOrCreate(config.robot().boards(0));
  ASSERT_TRUE(board.ok()) << board.status();
  auto channel = (*board)->OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  ASSERT_TRUE((*channel)->Enable().ok());
  drop_targets = true;
  auto pending = std::async(std::launch::async, [&] {
    return (*channel)->SetTarget(robot::board::TargetMode::kVelocity, 100);
  });
  ASSERT_TRUE(Await([&] { return targets.load() > 0; }));
  const auto begin = Clock::now();
  ASSERT_TRUE((*board)->EmergencyStop().ok());
  RecordProperty(
      "software_stop_ack_us",
      std::to_string(
          std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - begin).count()));
  EXPECT_LT(Clock::now() - begin, 100ms);
  EXPECT_FALSE(pending.get().ok());
  EXPECT_FALSE(Enabled());
  EXPECT_FALSE((*channel)->Enable().ok());
}

TEST_F(MixedBindingTest, RosEstopPreemptsBlockedCommandAndWatchdogWithTwoExecutorThreads) {
  const std::string name = "joshua_stop_" + std::to_string(getpid()) + "_" +
                           std::to_string(Clock::now().time_since_epoch().count());
  auto local = config;
  local.mutable_robot()->clear_perceptions();
  auto* policy = local.mutable_robot()->mutable_boards(0)->mutable_joshua_wire_host_policy();
  policy->set_operation_timeout_ms(2000);
  policy->set_attempt_timeout_ms(1000);
  local.mutable_robot()->mutable_boards(0)->mutable_firmware()->set_communication_watchdog_us(
      5000000);
  auto* action = local.mutable_robot()->mutable_actions()->mutable_single_actions(0);
  action->mutable_actuator()->set_actuator_name(name);
  for (auto& sub : *action->mutable_node()->mutable_subscriptions())
    sub.set_topic(name + sub.topic().substr(sub.topic().find_last_of('/')));
  ASSERT_TRUE(config::ValidateConfig(local).ok()) << config::ValidateConfig(local);
  auto runtime = std::make_shared<McuBus>(name, 1, local);
  auto client = std::make_shared<rclcpp::Node>(name + "_client");
  auto enable = client->create_publisher<std_msgs::msg::Bool>(name + "/torque", 1);
  auto command = client->create_publisher<std_msgs::msg::Float32>(name + "/position", 1);
  auto stop = client->create_client<std_srvs::srv::Trigger>("/" + name + "/estop");
  rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 2);
  executor.add_node(runtime);
  executor.add_node(client);
  std::thread spin([&] { executor.spin(); });
  const bool ready = Await(
      [&] {
        return enable->get_subscription_count() && command->get_subscription_count() &&
               stop->service_is_ready();
      },
      5000ms);
  EXPECT_TRUE(ready);
  if (ready) {
    std_msgs::msg::Bool on;
    on.data = true;
    enable->publish(on);
    const bool enabled = Await([&] { return Enabled(); });
    EXPECT_TRUE(enabled);
    if (enabled) {
      drop_targets = true;
      std_msgs::msg::Float32 target;
      target.data = 3;
      command->publish(target);
      const bool pending = Await([&] { return targets.load() > 0; });
      EXPECT_TRUE(pending);
      if (pending) {
        // Give the 10 ms watchdog time to become runnable behind the command.
        std::this_thread::sleep_for(30ms);
        auto result = stop->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
        EXPECT_EQ(result.wait_for(300ms), std::future_status::ready);
        EXPECT_GT(estops.load(), 0);
        if (result.wait_for(0ms) == std::future_status::ready) {
          EXPECT_TRUE(result.get()->success);
        }
        EXPECT_FALSE(Enabled());
        EXPECT_EQ(targets.load(), 1);  // No retry of the preempted target.
        enable->publish(on);
        std::this_thread::sleep_for(30ms);
        EXPECT_FALSE(Enabled());
      }
    }
  }
  executor.cancel();
  spin.join();
  executor.remove_node(runtime);
  executor.remove_node(client);
  runtime.reset();
}

TEST_F(MixedBindingTest, OperatorRecoveryProbeClearsLatchButNeverEnablesOrRestoresStream) {
  auto board = robot::board::BoardFactory::GetOrCreate(config.robot().boards(0));
  ASSERT_TRUE(board.ok()) << board.status();
  auto stream = (*board)->SubscribeScalar(128, 10000);
  ASSERT_TRUE(stream.ok());
  ASSERT_TRUE((*board)->EmergencyStop().ok());
  robot::board::BoardFactory::ResetForTesting();
  robot::comm::CommFactory::ResetSerialTransportCacheForTesting();
  auto recovery = config.robot().boards(0);
  recovery.mutable_comm()->set_transport_type(robot::comm::MESSAGE);
  recovery.clear_joshua_wire_host_policy();
  auto comm = robot::comm::CommFactory::CreateComm(recovery.comm());
  ASSERT_TRUE(comm.ok()) << comm.status();
  auto message = robot::comm::GetCommTransport<robot::comm::MessageTransport>(*comm);
  ASSERT_TRUE(message.ok());
  robot::board::diagnostics::SerialValidationOptions options;
  options.mode = "configure";
  options.channel = 0;
  options.allow_recover = true;
  options.settle_ms = 0;
  std::ostringstream report;
  const auto status =
      robot::board::diagnostics::RunSerialValidation(recovery, options, *message, report);
  ASSERT_TRUE(status.ok()) << status << '\n' << report.str();
  EXPECT_FALSE((*stream)->Latest().ok());
  std::lock_guard lock(firmware_mutex);
  EXPECT_FALSE(StepDirDeviceRuntime()->estopped);
  EXPECT_FALSE(g_channels[0].configured);
  EXPECT_FALSE(g_channels[0].enabled);
  EXPECT_FALSE(mixed.sensor.subscription.state.active);
  EXPECT_FALSE(mixed.watchdogs.configured);
}

TEST_F(MixedBindingTest, SensorFailureIsObservableAndDoesNotBlockControlOrSharedConsumer) {
  auto board = robot::board::BoardFactory::GetOrCreate(config.robot().boards(0));
  ASSERT_TRUE(board.ok()) << board.status();
  auto first = (*board)->SubscribeScalar(128, 10000);
  auto second = (*board)->SubscribeScalar(128, 10000);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  ASSERT_TRUE(Await([&] {
    auto s = (*second)->Latest();
    return s.ok() && s->usable;
  }));
  ASSERT_TRUE((*first)->Close().ok());
  adc_ready = false;
  ASSERT_TRUE(Await([&] {
    auto s = (*second)->Latest();
    return s.ok() && s->validity == JW_SAMPLE_INVALID && !s->usable;
  }));
  auto channel = (*board)->OpenChannel(0);
  ASSERT_TRUE(channel.ok());
  ASSERT_TRUE((*channel)->Enable().ok());
  ASSERT_TRUE((*channel)->SetTarget(robot::board::TargetMode::kPosition, 3).ok());
  EXPECT_TRUE(Enabled());
  ASSERT_TRUE((*second)->Close().ok());
  EXPECT_TRUE(Enabled());
}

TEST_F(MixedBindingTest, RebootAndMissingHeartbeatRequireDeliberateNewGeneration) {
  auto board = robot::board::BoardFactory::GetOrCreate(config.robot().boards(0));
  ASSERT_TRUE(board.ok()) << board.status();
  auto sensor = (*board)->SubscribeScalar(128, 10000);
  ASSERT_TRUE(sensor.ok());
  ASSERT_TRUE(Await([&] { return (*sensor)->Latest().ok(); }));
  {
    std::lock_guard lock(firmware_mutex);
    Boot(5678);
  }
  ASSERT_TRUE(Await([&] { return !(*sensor)->Latest().ok(); }));
  robot::board::BoardFactory::ResetForTesting();
  robot::comm::CommFactory::ResetSerialTransportCacheForTesting();
  auto next = robot::board::BoardFactory::GetOrCreate(config.robot().boards(0));
  ASSERT_TRUE(next.ok()) << next.status();
  EXPECT_FALSE(Enabled());
  EXPECT_FALSE((*sensor)->Latest().ok());
  auto fresh = (*next)->SubscribeScalar(128, 10000);
  ASSERT_TRUE(fresh.ok());
  ASSERT_TRUE(Await([&] {
    auto s = (*fresh)->Latest();
    return s.ok() && s->boot_id == 5678;
  }));
  drop_all = true;
  ASSERT_TRUE(Await([&] { return !(*fresh)->Latest().ok(); }));
}

TEST_F(MixedBindingTest, StandaloneSubscriberHonorsExplicitEnableAndDisablesOnTeardown) {
  const std::string name = "joshua_standalone_" + std::to_string(getpid()) + "_" +
                           std::to_string(Clock::now().time_since_epoch().count());
  auto local = config;
  local.mutable_robot()->clear_perceptions();
  auto* action = local.mutable_robot()->mutable_actions()->mutable_single_actions(0);
  action->mutable_node()->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
  action->mutable_actuator()->set_actuator_name(name);
  for (auto& sub : *action->mutable_node()->mutable_subscriptions())
    sub.set_topic(name + sub.topic().substr(sub.topic().find_last_of('/')));
  ASSERT_TRUE(action->actuator().stepper_config().require_explicit_enable());
  ASSERT_TRUE(config::ValidateConfig(local).ok());

  // Retain the board so a final lease release cannot hide skipped node teardown.
  auto board = robot::board::BoardFactory::GetOrCreate(local.robot().boards(0));
  ASSERT_TRUE(board.ok()) << board.status();
  auto runtime = std::make_shared<ActionSubscriber>(name, 1, local);
  EXPECT_FALSE(Enabled());
  auto client = std::make_shared<rclcpp::Node>(name + "_client");
  auto enable = client->create_publisher<std_msgs::msg::Bool>(name + "/torque", 1);
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(runtime);
  executor.add_node(client);
  ASSERT_TRUE(Await(
      [&] {
        executor.spin_some();
        return enable->get_subscription_count() == 1;
      },
      5000ms));
  std_msgs::msg::Bool enabled;
  enabled.data = true;
  enable->publish(enabled);
  EXPECT_TRUE(Await([&] {
    executor.spin_some();
    return Enabled();
  }));
  executor.remove_node(runtime);
  executor.remove_node(client);
  runtime.reset();
  EXPECT_FALSE(Enabled());
  std::lock_guard lock(firmware_mutex);
  EXPECT_TRUE(g_channels[0].configured);
  EXPECT_EQ(pins[4], 1);
}

class MixedRecorderQosTest
    : public MixedBindingTest,
      public ::testing::WithParamInterface<ros2::node::QosReliabilityPolicy> {};

TEST_P(MixedRecorderQosTest, RosBusStartsDisabledPublishesMetadataAndAcceptsExplicitEnableAndStop) {
  // Concurrent stacks must never send commands to each other or real runtimes.
  const std::string name = "joshua_test_" + std::to_string(getpid()) + "_" +
                           std::to_string(Clock::now().time_since_epoch().count());
  const std::string prefix = "/" + name + "/";
  auto local = config;
  const std::string actuator = name + "_stepper";
  for (auto& a : *local.mutable_robot()->mutable_actions()->mutable_single_actions()) {
    a.mutable_actuator()->set_actuator_name(actuator);
    for (auto& sub : *a.mutable_node()->mutable_subscriptions())
      sub.set_topic(actuator + sub.topic().substr(sub.topic().find_last_of('/')));
  }
  for (auto& s : *local.mutable_robot()->mutable_perceptions()->mutable_single_perceptions()) {
    s.mutable_node()->mutable_qos_setting()->set_reliability_policy(GetParam());
    for (auto& pub : *s.mutable_node()->mutable_publishers()) pub.set_topic(prefix + pub.topic());
  }
  ASSERT_TRUE(config::ValidateConfig(local).ok());
  auto runtime = std::make_shared<McuBus>(name, 1, local);
  EXPECT_FALSE(Enabled());
  auto client = std::make_shared<rclcpp::Node>(name + "_client");
  std::atomic<bool> observed{false};
  auto sub = client->create_subscription<std_msgs::msg::UInt8MultiArray>(
      prefix + "analog_a0/observation",
      rclcpp::QoS(1).best_effort(),
      [&](std_msgs::msg::UInt8MultiArray::ConstSharedPtr msg) {
        robot::perception::PerceptionPacket p;
        if (p.ParseFromArray(msg->data.data(), msg->data.size()) && p.scalar().usable() &&
            p.scalar().boot_id() == 1234)
          observed = true;
      });
  auto enable = client->create_publisher<std_msgs::msg::Bool>(actuator + "/torque", 1);
  auto stop = client->create_client<std_srvs::srv::Trigger>("/" + name + "/estop");
  rclcpp::executors::MultiThreadedExecutor executor(rclcpp::ExecutorOptions(), 2);
  executor.add_node(runtime);
  executor.add_node(client);
  std::thread spin([&] { executor.spin(); });
  EXPECT_TRUE(
      Await([&] { return observed.load() && enable->get_subscription_count() > 0; }, 5000ms));
  const std::string recorder = std::filesystem::absolute("tools/serial/record_scalar").string();
  const std::string capture =
      std::string(getenv("TEST_TMPDIR")) + "/scalar_" + std::to_string(GetParam()) + ".jsonl";
  std::vector<std::string> args = {recorder,
                                   "--topic",
                                   prefix + "analog_a0/observation",
                                   "--sensor",
                                   "analog_a0",
                                   "--seconds",
                                   "3",
                                   "--output",
                                   capture};
  std::vector<char*> argv;
  for (auto& arg : args) argv.push_back(arg.data());
  argv.push_back(nullptr);
  pid_t child;
  const int spawned = posix_spawn(&child, recorder.c_str(), nullptr, nullptr, argv.data(), environ);
  EXPECT_EQ(spawned, 0);
  if (spawned == 0) {
    int status = 0;
    EXPECT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    std::ifstream file(capture);
    std::string line;
    bool metadata = false;
    while (std::getline(file, line)) {
      if (line.find("\"boot_id\": 1234") != std::string::npos &&
          line.find("\"sensor\": \"analog_a0\"") != std::string::npos &&
          line.find("\"unit\": 65535") != std::string::npos)
        metadata = true;
    }
    EXPECT_TRUE(metadata);
  }
  std_msgs::msg::Bool enabled;
  enabled.data = true;
  enable->publish(enabled);
  EXPECT_TRUE(Await([&] { return Enabled(); }));
  auto result = stop->async_send_request(std::make_shared<std_srvs::srv::Trigger::Request>());
  EXPECT_EQ(result.wait_for(2s), std::future_status::ready);
  if (result.wait_for(0s) == std::future_status::ready) {
    EXPECT_TRUE(result.get()->success);
  }
  EXPECT_TRUE(Await([&] { return !Enabled(); }));
  executor.cancel();
  spin.join();
  executor.remove_node(runtime);
  executor.remove_node(client);
  runtime.reset();
}

INSTANTIATE_TEST_SUITE_P(
    PublisherReliability,
    MixedRecorderQosTest,
    ::testing::Values(ros2::node::QOS_RELIABILITY_POLICY_RELIABLE,
                      ros2::node::QOS_RELIABILITY_POLICY_BEST_EFFORT),
    [](const ::testing::TestParamInfo<ros2::node::QosReliabilityPolicy>& info) {
      return info.param == ros2::node::QOS_RELIABILITY_POLICY_RELIABLE ? "Reliable" : "BestEffort";
    });
}  // namespace
