#include "ros2/actuator_session.h"

#include <atomic>
#include <limits>
#include <thread>
#include <vector>

#include "google/protobuf/text_format.h"
#include "google/protobuf/util/json_util.h"
#include "gtest/gtest.h"
#include "mhs/ros_client.h"
#include "robot/action/motors/drivers/stepper_driver.h"
#include "ros2/actuator_command_endpoint.h"

namespace ros2_actuator {
namespace {
using google::protobuf::Struct;

config::Config Config() {
  config::Config c;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        hardware_api {
          enabled: true
          devices {
            actuator_name: "motor"
            description: "Test stepper"
            max_move_degrees: 30
            max_move_duration_ms: 100
            feedback_max_age_ms: 50
          }
        }
        robot {
          boards {
            name: "board"
            board_type: TEENSY41
            channels {
              index: 0
              drive: STEP_DIR
              step_dir {}
            }
          }
          actions {
            single_actions {
              action_type: ACTUATOR
              node {
                id: 1
                node_type: ACTUATOR_SUBSCRIBER
                subscriptions { topic: "/motor/command" ros2_data_type: STRING }
                publishers { topic: "/motor/status" ros2_data_type: STRING }
              }
              actuator {
                actuator_name: "motor"
                board_name: "board"
                channel: 0
                motor_type: MOTOR_STEPPER_NEMA17
                physical_lower_limit: -100
                physical_upper_limit: 100
                operational_lower_limit: -90
                operational_upper_limit: 90
                stepper_config { steps_per_degree: 10 gear_ratio: 1 manual_lifecycle: true }
              }
            }
          }
        }
      )pb",
      &c));
  return c;
}
Struct Request(const std::string& op, double position = 0) {
  Struct s;
  (*s.mutable_fields())["operation"].set_string_value(op);
  (*s.mutable_fields())["device_id"].set_string_value("motor");
  (*s.mutable_fields())["position_degrees"].set_number_value(position);
  return s;
}
std::string Status(const Struct& s) {
  return s.fields().at("status").string_value();
}
bool Failed(const Struct& s) {
  return s.fields().contains("error");
}

class FakeChannel : public robot::board::BoardChannel {
 public:
  std::vector<std::string> calls;
  std::atomic<float> position{0}, target{0};
  bool fail_read = false, fail_write = false, fail_disable = false;
  absl::Status Enable() override {
    calls.push_back("enable");
    return absl::OkStatus();
  }
  absl::Status Disable() override {
    calls.push_back("disable");
    return fail_disable ? absl::UnavailableError("lost disable ACK") : absl::OkStatus();
  }
  absl::Status SetTarget(robot::board::TargetMode mode, float value) override {
    EXPECT_EQ(mode, robot::board::TargetMode::kPosition);
    target = value;
    calls.push_back("target");
    return fail_write ? absl::UnavailableError("lost target ACK") : absl::OkStatus();
  }
  absl::StatusOr<robot::board::ChannelFeedback> ReadFeedback() override {
    calls.push_back("read");
    if (fail_read) return absl::UnavailableError("lost feedback");
    robot::board::ChannelFeedback f;
    f.position = position;
    return f;
  }
};

TEST(Config, RejectsMissingExposureBadLimitsAndUnsupportedHardware) {
  auto c = Config();
  ASSERT_TRUE(ResolveDevice(c).ok());
  c.mutable_hardware_api()->set_enabled(false);
  EXPECT_FALSE(ResolveDevice(c).ok());
  c = Config();
  c.mutable_hardware_api()->mutable_devices(0)->set_max_move_degrees(0);
  EXPECT_FALSE(ResolveDevice(c).ok());
  c = Config();
  c.mutable_robot()->mutable_boards(0)->set_board_type(robot::board::ESP32);
  EXPECT_FALSE(ResolveDevice(c).ok());
  c = Config();
  c.mutable_robot()
      ->mutable_actions()
      ->mutable_single_actions(0)
      ->mutable_actuator()
      ->set_operational_upper_limit(200);
  EXPECT_FALSE(ResolveDevice(c).ok());
  c = Config();
  *c.mutable_robot()->mutable_actions()->add_single_actions() =
      c.robot().actions().single_actions(0);
  EXPECT_FALSE(ResolveDevice(c).ok());
}

class ActuatorSessionTest : public testing::Test {
 protected:
  std::shared_ptr<FakeChannel> channel = std::make_shared<FakeChannel>();
  ActuatorSession runtime{ResolveDevice(Config()).value()};
  void Attach() {
    ASSERT_TRUE(runtime
                    .Attach(std::make_shared<robot::action::StepperDriver>(
                        channel, Config().robot().actions().single_actions(0).actuator()))
                    .ok());
    channel->calls.clear();
  }
};
TEST_F(ActuatorSessionTest, DiscoveryDoesNotAttachOrMove) {
  auto list = runtime.Handle(Request("list_devices"));
  EXPECT_EQ(list.fields().at("devices").list_value().values_size(), 1);
  EXPECT_FALSE(Failed(runtime.Handle(Request("describe_device"))));
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_TRUE(channel->calls.empty());
}
TEST_F(ActuatorSessionTest, AttachDisablesWithoutEnabling) {
  ASSERT_TRUE(runtime
                  .Attach(std::make_shared<robot::action::StepperDriver>(
                      channel, Config().robot().actions().single_actions(0).actuator()))
                  .ok());
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"disable", "read"}));
  EXPECT_EQ(Status(runtime.Handle(Request("read_state"))), "ready_disarmed");
}
TEST_F(ActuatorSessionTest, RejectsInvalidMovesWithoutWriting) {
  Attach();
  for (double value : {91.0,
                       31.0,
                       std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::quiet_NaN()}) {
    EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", value))));
  }
  for (const auto& call : channel->calls) EXPECT_EQ(call, "read");
  auto bad = Request("write_position");
  (*bad.mutable_fields())["position_degrees"].set_string_value("10");
  EXPECT_TRUE(Failed(runtime.Handle(bad)));
}
TEST_F(ActuatorSessionTest, QuantizesTracksCompletionAndRejectsOverlap) {
  Attach();
  auto accepted = runtime.Handle(Request("write_position", 10.04));
  EXPECT_EQ(Status(accepted), "moving");
  EXPECT_FLOAT_EQ(channel->target.load(), 100);
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"read", "target", "enable"}));
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 20))));
  runtime.Poll();
  EXPECT_EQ(Status(runtime.Handle(Request("read_state"))), "moving");
  channel->position = 100;
  runtime.Poll();
  auto state = runtime.Handle(Request("read_state"));
  EXPECT_EQ(Status(state), "controller_target_reached");
  EXPECT_FALSE(state.fields().at("physical_position_verified").bool_value());
  EXPECT_FALSE(Failed(runtime.Handle(Request("write_position", 20))));
}
TEST_F(ActuatorSessionTest, StopInvalidatesReferenceWithoutMovingToIdle) {
  Attach();
  ASSERT_FALSE(Failed(runtime.Handle(Request("write_position", 10))));
  channel->calls.clear();
  auto stopped = runtime.Handle(Request("stop_device"));
  EXPECT_EQ(Status(stopped), "stopped");
  EXPECT_TRUE(stopped.fields().at("disable_acknowledged").bool_value());
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"disable"}));
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 0))));
}
TEST_F(ActuatorSessionTest, TimeoutDoesNotNeedClientPolling) {
  Attach();
  ASSERT_FALSE(Failed(runtime.Handle(Request("write_position", 10))));
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  runtime.Poll();
  EXPECT_EQ(Status(runtime.Handle(Request("read_state"))), "timed_out");
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 0))));
}
TEST_F(ActuatorSessionTest, AmbiguousWriteDisablesAndNeverRetries) {
  Attach();
  channel->fail_write = true;
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"read", "target", "disable"}));
  channel->calls.clear();
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_TRUE(channel->calls.empty());
}
TEST_F(ActuatorSessionTest, LostFeedbackAndDisableAckAreNotReportedAsStopped) {
  Attach();
  ASSERT_FALSE(Failed(runtime.Handle(Request("write_position", 10))));
  channel->fail_read = true;
  channel->fail_disable = true;
  runtime.Poll();
  auto state = runtime.Handle(Request("read_state"));
  EXPECT_EQ(Status(state), "stop_unconfirmed");
  EXPECT_FALSE(state.fields().at("disable_acknowledged").bool_value());
  EXPECT_FALSE(state.fields().at("feedback_valid").bool_value());
}
TEST_F(ActuatorSessionTest, RejectsRoundedTargetOutsideLimits) {
  auto c = Config();
  c.mutable_robot()
      ->mutable_actions()
      ->mutable_single_actions(0)
      ->mutable_actuator()
      ->set_operational_upper_limit(0.06);
  ActuatorSession narrow(ResolveDevice(c).value());
  ASSERT_TRUE(narrow
                  .Attach(std::make_shared<robot::action::StepperDriver>(
                      channel, c.robot().actions().single_actions(0).actuator()))
                  .ok());
  channel->calls.clear();
  EXPECT_TRUE(Failed(narrow.Handle(Request("write_position", 0.06))));
  EXPECT_TRUE(channel->calls.empty());
}
TEST_F(ActuatorSessionTest, DetectsCounterResetBeforeNextWrite) {
  Attach();
  ASSERT_FALSE(Failed(runtime.Handle(Request("write_position", 10))));
  channel->position = 100;
  runtime.Poll();
  channel->position = 0;
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_EQ(Status(runtime.Handle(Request("read_state"))), "fault");
}
TEST_F(ActuatorSessionTest, ConcurrentCommandsAcceptOnlyOne) {
  Attach();
  Struct a, b;
  std::thread first([&] { a = runtime.Handle(Request("write_position", 10)); });
  std::thread second([&] { b = runtime.Handle(Request("write_position", 20)); });
  first.join();
  second.join();
  EXPECT_NE(Failed(a), Failed(b));
}
TEST_F(ActuatorSessionTest, InvalidFeedbackCannotAuthorizeMotion) {
  Attach();
  channel->position = std::numeric_limits<float>::quiet_NaN();
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"read", "disable"}));
}
TEST_F(ActuatorSessionTest, StartupOutsideLimitsReportsCountAndCannotMove) {
  channel->position = 29940;
  const auto status = runtime.Attach(std::make_shared<robot::action::StepperDriver>(
      channel, Config().robot().actions().single_actions(0).actuator()));
  EXPECT_EQ(status.code(), absl::StatusCode::kOutOfRange);
  EXPECT_NE(std::string(status.message()).find("29940"), std::string::npos);
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"disable", "read"}));
  channel->calls.clear();
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 5))));
  EXPECT_TRUE(channel->calls.empty());
}
TEST_F(ActuatorSessionTest, DestructionDisablesWithoutTargetCommand) {
  {
    ActuatorSession local(ResolveDevice(Config()).value());
    ASSERT_TRUE(local
                    .Attach(std::make_shared<robot::action::StepperDriver>(
                        channel, Config().robot().actions().single_actions(0).actuator()))
                    .ok());
    channel->calls.clear();
  }
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"disable"}));
}

TEST(Config, RejectsMalformedTopicsAndAlternateCommandOwners) {
  for (const auto& topic : {"relative/command", "/motor//command", "/motor/", "/motor/2command"}) {
    auto config = Config();
    config.mutable_robot()
        ->mutable_actions()
        ->mutable_single_actions(0)
        ->mutable_node()
        ->mutable_subscriptions(0)
        ->set_topic(topic);
    EXPECT_FALSE(ResolveDevice(config).ok()) << topic;
  }
  auto config = Config();
  auto other = config.robot().actions().single_actions(0);
  other.mutable_actuator()->set_actuator_name("other");
  *config.mutable_robot()->mutable_actions()->add_single_actions() = other;
  EXPECT_FALSE(ResolveDevice(config).ok());
}

TEST(Config, RejectsMissingTopicsRetainedCommandsAndAutomaticLifecycle) {
  auto c = Config();
  c.mutable_robot()
      ->mutable_actions()
      ->mutable_single_actions(0)
      ->mutable_node()
      ->clear_subscriptions();
  EXPECT_FALSE(ResolveDevice(c).ok());
  c = Config();
  c.mutable_robot()
      ->mutable_actions()
      ->mutable_single_actions(0)
      ->mutable_node()
      ->mutable_qos_setting()
      ->set_durability_policy(ros2::node::QOS_DURABILITY_POLICY_TRANSIENT_LOCAL);
  EXPECT_FALSE(ResolveDevice(c).ok());
  c = Config();
  c.mutable_robot()
      ->mutable_actions()
      ->mutable_single_actions(0)
      ->mutable_actuator()
      ->mutable_stepper_config()
      ->set_manual_lifecycle(false);
  EXPECT_FALSE(ResolveDevice(c).ok());
}

class RosPipelineTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    const char* temp = std::getenv("TEST_TMPDIR");
    if (temp) setenv("ROS_LOG_DIR", temp, 1);
    setenv("ROS_DOMAIN_ID", "173", 1);
    rclcpp::init(0, nullptr);
  }
  static void TearDownTestSuite() {
    rclcpp::shutdown();
  }
  void SetUp() override {
    auto config = Config();
    // Discovery can take longer than a move: allow time for the success test.
    config.mutable_hardware_api()->mutable_devices(0)->set_max_move_duration_ms(500);
    device = ResolveDevice(config).value();
    channel = std::make_shared<FakeChannel>();
    node = std::make_shared<rclcpp::Node>("test_actuator_owner");
    auto driver = std::make_shared<robot::action::StepperDriver>(channel, device.actuator);
    ASSERT_TRUE(driver->Init().ok());
    endpoint = std::make_unique<CommandEndpoint>(*node, device, driver);
    executor.add_node(node);
    thread = std::thread([this] { executor.spin(); });
    client = std::make_unique<mhs::RosClient>(device);
  }
  void TearDown() override {
    client.reset();
    executor.cancel();
    thread.join();
    endpoint.reset();
    node.reset();
  }
  Device device;
  std::shared_ptr<FakeChannel> channel;
  std::shared_ptr<rclcpp::Node> node;
  std::unique_ptr<CommandEndpoint> endpoint;
  rclcpp::executors::SingleThreadedExecutor executor;
  std::thread thread;
  std::unique_ptr<mhs::RosClient> client;
};

TEST_F(RosPipelineTest, ConfiguredTopicsReachDriverAndReturnAcknowledgedState) {
  const auto description = client->Request(Request("describe_device"));
  ASSERT_FALSE(Failed(description));
  EXPECT_TRUE(description.fields().at("connected").bool_value());
  auto accepted = client->Request(Request("write_position", 10.04));
  ASSERT_FALSE(Failed(accepted));
  EXPECT_TRUE(accepted.fields().at("accepted").bool_value());
  EXPECT_FLOAT_EQ(channel->target.load(), 100);
  EXPECT_TRUE(Failed(client->Request(Request("write_position", 20))));
  channel->position = 100;
  auto state = client->Request(Request("read_state"));
  ASSERT_FALSE(Failed(state));
  EXPECT_EQ(Status(state), "controller_target_reached");
  auto stopped = client->Stop();
  ASSERT_FALSE(Failed(stopped));
  EXPECT_TRUE(stopped.fields().at("disable_acknowledged").bool_value());
  EXPECT_FALSE(stopped.fields().at("reference_valid").bool_value());
  EXPECT_FLOAT_EQ(channel->target.load(), 100);  // Stop did not move to idle.
  EXPECT_TRUE(Failed(client->Request(Request("write_position", 0))));
}

TEST_F(RosPipelineTest, NodeMonitorsTimeoutAfterClientDisappears) {
  ASSERT_FALSE(Failed(client->Request(Request("write_position", 10))));
  client.reset();
  std::this_thread::sleep_for(std::chrono::milliseconds(600));
  client = std::make_unique<mhs::RosClient>(device);
  auto state = client->Request(Request("read_state"));
  ASSERT_FALSE(Failed(state));
  EXPECT_EQ(Status(state), "timed_out");
  EXPECT_TRUE(state.fields().at("disable_acknowledged").bool_value());
}

TEST_F(RosPipelineTest, DuplicateExpiredAndPreviousSessionCommandsNeverExecute) {
  // Exercise the public ROS protocol, including replay of a completed command.
  auto raw = std::make_shared<rclcpp::Node>("test_raw_client");
  auto pub = raw->create_publisher<std_msgs::msg::String>(device.command_topic, 10);
  Struct response;
  bool received = false;
  std::string expected_id;
  auto sub = raw->create_subscription<std_msgs::msg::String>(
      device.status_topic, 10, [&](std_msgs::msg::String::ConstSharedPtr msg) {
        Struct parsed;
        ASSERT_TRUE(google::protobuf::util::JsonStringToMessage(msg->data, &parsed).ok());
        const auto id = parsed.fields().find("request_id");
        if (id == parsed.fields().end() || id->second.string_value() != expected_id) return;
        response = std::move(parsed);
        received = true;
      });
  rclcpp::executors::SingleThreadedExecutor raw_executor;
  raw_executor.add_node(raw);
  const auto discovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
  while (pub->get_subscription_count() != 1 || sub->get_publisher_count() != 1) {
    ASSERT_LT(std::chrono::steady_clock::now(), discovery_deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  auto send = [&](Struct request) {
    received = false;
    expected_id = request.fields().at("request_id").string_value();
    std_msgs::msg::String msg;
    EXPECT_TRUE(google::protobuf::util::MessageToJsonString(request, &msg.data).ok());
    pub->publish(msg);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!received && std::chrono::steady_clock::now() < deadline) {
      raw_executor.spin_some();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(received);
    return response;
  };
  auto request = Request("describe_device");
  (*request.mutable_fields())["request_id"].set_string_value("discovery");
  auto description = send(request);
  ASSERT_FALSE(Failed(description));
  request = Request("write_position", 10);
  auto& fields = *request.mutable_fields();
  fields["session_id"] = description.fields().at("session_id");
  fields["request_id"].set_string_value("move-1");
  auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                 std::chrono::system_clock::now().time_since_epoch())
                 .count();
  fields["expires_unix_ms"].set_number_value(now - 1);
  EXPECT_TRUE(Failed(send(request)));
  EXPECT_EQ(channel->target.load(), 0);
  fields["expires_unix_ms"].set_number_value(now + 1000);
  fields["session_id"].set_string_value("previous-session");
  EXPECT_TRUE(Failed(send(request)));
  EXPECT_EQ(channel->target.load(), 0);
  fields["session_id"] = description.fields().at("session_id");
  auto first = send(request);
  ASSERT_FALSE(Failed(first));
  channel->position = 100;
  ASSERT_FALSE(Failed(client->Request(Request("read_state"))));
  fields["position_degrees"].set_number_value(20);
  auto replay = send(request);
  ASSERT_FALSE(Failed(replay));
  ASSERT_TRUE(replay.fields().contains("command_id"));
  EXPECT_EQ(replay.fields().at("command_id").string_value(),
            first.fields().at("command_id").string_value());
  EXPECT_EQ(channel->target.load(), 100);
}
}  // namespace
}  // namespace ros2_actuator
