#include "robot/action/motors/drivers/ti_demo_driver.h"

#include <memory>
#include <vector>

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
    return set_target_status_;
  }
  absl::StatusOr<robot::board::ChannelFeedback> ReadFeedback() override {
    return robot::board::ChannelFeedback{};
  }

  int enable_calls_ = 0;
  int disable_calls_ = 0;
  int set_target_calls_ = 0;
  robot::board::TargetMode last_mode_ = robot::board::TargetMode::kPosition;
  float last_value_ = 0.0f;
  absl::Status set_target_status_ = absl::OkStatus();
};

robot::action::Actuator MakeJointActuator() {
  robot::action::Actuator actuator;
  actuator.set_actuator_name("joint_1");
  actuator.set_id(7);
  actuator.set_motor_type(robot::action::MotorType::MOTOR_TI_DEMO);
  actuator.set_board_name("am243_1");
  actuator.set_channel(0);
  actuator.set_physical_lower_limit(-180.0f);
  actuator.set_physical_upper_limit(180.0f);
  actuator.set_operational_lower_limit(-90.0f);
  actuator.set_operational_upper_limit(90.0f);
  actuator.mutable_am243_ethercat_config()->set_idle_position(0.0f);
  return actuator;
}

TEST(TiDemoDriverTest, InitRejectsNullChannel) {
  TiDemoDriver driver(nullptr, MakeJointActuator());

  EXPECT_EQ(driver.Init().code(), absl::StatusCode::kInvalidArgument);
}

TEST(TiDemoDriverTest, InitEnablesChannel) {
  auto channel = std::make_shared<RecordingChannel>();
  TiDemoDriver driver(channel, MakeJointActuator());

  ASSERT_TRUE(driver.Init().ok());

  EXPECT_EQ(channel->enable_calls_, 1);
}

TEST(TiDemoDriverTest, SetCommandsValidateInputs) {
  auto channel = std::make_shared<RecordingChannel>();
  TiDemoDriver driver(channel, MakeJointActuator());
  ASSERT_TRUE(driver.Init().ok());

  EXPECT_EQ(driver.SetSpeed(-1.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(driver.SetTorque(-1.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(driver.SetPosition(100.0f).code(), absl::StatusCode::kInvalidArgument);
  // Rejected commands never reach the channel.
  EXPECT_EQ(channel->set_target_calls_, 0);
}

TEST(TiDemoDriverTest, SetPositionSendsNormalizedNativeTarget) {
  auto channel = std::make_shared<RecordingChannel>();
  TiDemoDriver driver(channel, MakeJointActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetPosition(0.0f).ok());

  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kPosition);
  // Midpoint of [-90, 90] normalizes to half the native 0-255 range; the
  // channel owns the final rounding to a seed byte.
  EXPECT_FLOAT_EQ(channel->last_value_, 127.5f);
}

TEST(TiDemoDriverTest, SetSpeedPassesNativeValueThrough) {
  auto channel = std::make_shared<RecordingChannel>();
  TiDemoDriver driver(channel, MakeJointActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetSpeed(3.2f).ok());

  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kVelocity);
  EXPECT_FLOAT_EQ(channel->last_value_, 3.2f);
}

TEST(TiDemoDriverTest, SetTorqueScalesToNativeFullScale) {
  auto channel = std::make_shared<RecordingChannel>();
  TiDemoDriver driver(channel, MakeJointActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetTorque(1.0f).ok());

  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kTorque);
  EXPECT_FLOAT_EQ(channel->last_value_, 255.0f);
}

TEST(TiDemoDriverTest, SetActionRoutesPresetTeardownToHarmlessTeardown) {
  auto channel = std::make_shared<RecordingChannel>();
  TiDemoDriver driver(channel, MakeJointActuator());
  ASSERT_TRUE(driver.Init().ok());

  robot::action::ActionPacket packet;
  packet.set_preset(robot::action::PresetCommand::PRESET_TEARDOWN);

  EXPECT_TRUE(driver.SetAction(packet).ok());
}

TEST(TiDemoDriverTest, SetActionSurfacesChannelFailure) {
  auto channel = std::make_shared<RecordingChannel>();
  channel->set_target_status_ = absl::Status(absl::StatusCode::kUnavailable, "WKC mismatch");
  TiDemoDriver driver(channel, MakeJointActuator());
  ASSERT_TRUE(driver.Init().ok());

  robot::action::ActionPacket packet;
  packet.set_position(0.0f);

  EXPECT_EQ(driver.SetAction(packet).code(), absl::StatusCode::kUnavailable);
}

}  // namespace
}  // namespace robot::action
