#include "mhs/runtime.h"

#include <limits>
#include <thread>
#include <vector>

#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

namespace mhs {
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
              actuator {
                actuator_name: "motor"
                board_name: "board"
                channel: 0
                motor_type: MOTOR_STEPPER_NEMA17
                physical_lower_limit: -100
                physical_upper_limit: 100
                operational_lower_limit: -90
                operational_upper_limit: 90
                stepper_config { steps_per_degree: 10 gear_ratio: 1 }
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
  float position = 0, target = 0;
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

class RuntimeTest : public testing::Test {
 protected:
  std::shared_ptr<FakeChannel> channel = std::make_shared<FakeChannel>();
  Runtime runtime{ResolveDevice(Config()).value()};
  void Attach() {
    ASSERT_TRUE(runtime.Attach(channel).ok());
    channel->calls.clear();
  }
};
TEST_F(RuntimeTest, DiscoveryDoesNotAttachOrMove) {
  auto list = runtime.Handle(Request("list_devices"));
  EXPECT_EQ(list.fields().at("devices").list_value().values_size(), 1);
  EXPECT_FALSE(Failed(runtime.Handle(Request("describe_device"))));
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_TRUE(channel->calls.empty());
}
TEST_F(RuntimeTest, AttachDisablesWithoutEnabling) {
  ASSERT_TRUE(runtime.Attach(channel).ok());
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"disable", "read"}));
  EXPECT_EQ(Status(runtime.Handle(Request("read_state"))), "ready_disarmed");
}
TEST_F(RuntimeTest, RejectsInvalidMovesWithoutWriting) {
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
TEST_F(RuntimeTest, QuantizesTracksCompletionAndRejectsOverlap) {
  Attach();
  auto accepted = runtime.Handle(Request("write_position", 10.04));
  EXPECT_EQ(Status(accepted), "moving");
  EXPECT_FLOAT_EQ(channel->target, 100);
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
TEST_F(RuntimeTest, StopInvalidatesReferenceWithoutMovingToIdle) {
  Attach();
  ASSERT_FALSE(Failed(runtime.Handle(Request("write_position", 10))));
  channel->calls.clear();
  auto stopped = runtime.Handle(Request("stop_device"));
  EXPECT_EQ(Status(stopped), "stopped");
  EXPECT_TRUE(stopped.fields().at("disable_acknowledged").bool_value());
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"disable"}));
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 0))));
}
TEST_F(RuntimeTest, TimeoutDoesNotNeedClientPolling) {
  Attach();
  ASSERT_FALSE(Failed(runtime.Handle(Request("write_position", 10))));
  std::this_thread::sleep_for(std::chrono::milliseconds(120));
  runtime.Poll();
  EXPECT_EQ(Status(runtime.Handle(Request("read_state"))), "timed_out");
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 0))));
}
TEST_F(RuntimeTest, AmbiguousWriteDisablesAndNeverRetries) {
  Attach();
  channel->fail_write = true;
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"read", "target", "disable"}));
  channel->calls.clear();
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_TRUE(channel->calls.empty());
}
TEST_F(RuntimeTest, LostFeedbackAndDisableAckAreNotReportedAsStopped) {
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
TEST_F(RuntimeTest, RejectsRoundedTargetOutsideLimits) {
  auto c = Config();
  c.mutable_robot()
      ->mutable_actions()
      ->mutable_single_actions(0)
      ->mutable_actuator()
      ->set_operational_upper_limit(0.06);
  Runtime narrow(ResolveDevice(c).value());
  ASSERT_TRUE(narrow.Attach(channel).ok());
  channel->calls.clear();
  EXPECT_TRUE(Failed(narrow.Handle(Request("write_position", 0.06))));
  EXPECT_TRUE(channel->calls.empty());
}
TEST_F(RuntimeTest, DetectsCounterResetBeforeNextWrite) {
  Attach();
  ASSERT_FALSE(Failed(runtime.Handle(Request("write_position", 10))));
  channel->position = 100;
  runtime.Poll();
  channel->position = 0;
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_EQ(Status(runtime.Handle(Request("read_state"))), "fault");
}
TEST_F(RuntimeTest, ConcurrentCommandsAcceptOnlyOne) {
  Attach();
  Struct a, b;
  std::thread first([&] { a = runtime.Handle(Request("write_position", 10)); });
  std::thread second([&] { b = runtime.Handle(Request("write_position", 20)); });
  first.join();
  second.join();
  EXPECT_NE(Failed(a), Failed(b));
}
TEST_F(RuntimeTest, InvalidFeedbackCannotAuthorizeMotion) {
  Attach();
  channel->position = std::numeric_limits<float>::quiet_NaN();
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 10))));
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"read", "disable"}));
}
TEST_F(RuntimeTest, StartupOutsideLimitsReportsCountAndCannotMove) {
  channel->position = 29940;
  const auto status = runtime.Attach(channel);
  EXPECT_EQ(status.code(), absl::StatusCode::kOutOfRange);
  EXPECT_NE(std::string(status.message()).find("29940"), std::string::npos);
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"disable", "read"}));
  channel->calls.clear();
  EXPECT_TRUE(Failed(runtime.Handle(Request("write_position", 5))));
  EXPECT_TRUE(channel->calls.empty());
}
TEST_F(RuntimeTest, DestructionDisablesWithoutTargetCommand) {
  {
    Runtime local(ResolveDevice(Config()).value());
    ASSERT_TRUE(local.Attach(channel).ok());
    channel->calls.clear();
  }
  EXPECT_EQ(channel->calls, (std::vector<std::string>{"disable"}));
}
}  // namespace
}  // namespace mhs
