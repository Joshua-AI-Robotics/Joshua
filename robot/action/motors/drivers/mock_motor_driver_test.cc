#include "robot/action/motors/drivers/mock_motor_driver.h"

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/board/interfaces/board_channel.h"

namespace robot::action {
namespace {

class RecordingChannel : public robot::board::BoardChannel {
 public:
  absl::Status Enable() override {
    enable_calls_++;
    return absl::OkStatus();
  }
  absl::Status Disable() override {
    disable_calls_++;
    return absl::OkStatus();
  }
  absl::Status SetTarget(robot::board::TargetMode mode, float value) override {
    last_mode_ = mode;
    last_value_ = value;
    set_target_calls_++;
    return absl::OkStatus();
  }
  absl::StatusOr<robot::board::ChannelFeedback> ReadFeedback() override {
    return robot::board::ChannelFeedback{};
  }

  int enable_calls_ = 0;
  int disable_calls_ = 0;
  int set_target_calls_ = 0;
  robot::board::TargetMode last_mode_ = robot::board::TargetMode::kPosition;
  float last_value_ = 0.0f;
};

robot::action::Actuator MakeMockActuator() {
  robot::action::Actuator actuator;
  actuator.set_actuator_name("mock_motor_1");
  actuator.set_id(1);
  actuator.set_motor_type(robot::action::MotorType::MOTOR_MOCK);
  actuator.set_board_name("mock_actuator_board");
  actuator.set_channel(0);
  actuator.set_operational_lower_limit(-90.0f);
  actuator.set_operational_upper_limit(90.0f);
  actuator.mutable_mock_motor_config()->set_motor_id(1);
  return actuator;
}

TEST(MockMotorDriverTest, InitRejectsNullChannel) {
  MockMotorDriver driver(nullptr, MakeMockActuator());

  EXPECT_EQ(driver.Init().code(), absl::StatusCode::kInvalidArgument);
}

TEST(MockMotorDriverTest, InitEnablesChannel) {
  auto channel = std::make_shared<RecordingChannel>();
  MockMotorDriver driver(channel, MakeMockActuator());

  ASSERT_TRUE(driver.Init().ok());

  EXPECT_EQ(channel->enable_calls_, 1);
}

TEST(MockMotorDriverTest, GetIdUsesActuatorName) {
  MockMotorDriver driver(nullptr, MakeMockActuator());
  EXPECT_EQ(driver.GetId(), "mock_motor_driver_mock_motor_1");
}

TEST(MockMotorDriverTest, SetPositionForwardsValueUnchanged) {
  auto channel = std::make_shared<RecordingChannel>();
  MockMotorDriver driver(channel, MakeMockActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetPosition(45.0f).ok());

  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kPosition);
  EXPECT_FLOAT_EQ(channel->last_value_, 45.0f);
}

TEST(MockMotorDriverTest, SetSpeedForwardsValueUnchanged) {
  auto channel = std::make_shared<RecordingChannel>();
  MockMotorDriver driver(channel, MakeMockActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetSpeed(12.5f).ok());

  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kVelocity);
  EXPECT_FLOAT_EQ(channel->last_value_, 12.5f);
}

TEST(MockMotorDriverTest, SetTorqueGatesEnableDisable) {
  auto channel = std::make_shared<RecordingChannel>();
  MockMotorDriver driver(channel, MakeMockActuator());
  ASSERT_TRUE(driver.Init().ok());  // 1 enable call already, from Init().

  ASSERT_TRUE(driver.SetTorque(0.0f).ok());
  EXPECT_EQ(channel->disable_calls_, 1);

  ASSERT_TRUE(driver.SetTorque(1.0f).ok());
  EXPECT_EQ(channel->enable_calls_, 2);
}

TEST(MockMotorDriverTest, SetTorqueRejectsNegativeValue) {
  auto channel = std::make_shared<RecordingChannel>();
  MockMotorDriver driver(channel, MakeMockActuator());
  ASSERT_TRUE(driver.Init().ok());  // 1 enable call already, from Init().

  EXPECT_EQ(driver.SetTorque(-1.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(channel->enable_calls_, 1);
  EXPECT_EQ(channel->disable_calls_, 0);
}

TEST(MockMotorDriverTest, SetActionRoutesMiddlePositionPresetToOperationalMidpoint) {
  auto channel = std::make_shared<RecordingChannel>();
  MockMotorDriver driver(channel, MakeMockActuator());
  ASSERT_TRUE(driver.Init().ok());

  robot::action::ActionPacket packet;
  packet.set_preset(robot::action::PresetCommand::PRESET_MIDDLE_POSITION);
  ASSERT_TRUE(driver.SetAction(packet).ok());

  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kPosition);
  EXPECT_FLOAT_EQ(channel->last_value_, 0.0f);  // midpoint of [-90, 90]
}

TEST(MockMotorDriverTest, TeardownDisablesChannel) {
  auto channel = std::make_shared<RecordingChannel>();
  MockMotorDriver driver(channel, MakeMockActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.Teardown().ok());

  EXPECT_EQ(channel->disable_calls_, 1);
}

}  // namespace
}  // namespace robot::action
