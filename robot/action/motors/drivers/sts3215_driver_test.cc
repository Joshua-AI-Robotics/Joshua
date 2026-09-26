#include "robot/action/motors/drivers/sts3215_driver.h"

#include <limits>
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

robot::action::Actuator MakeServoActuator() {
  robot::action::Actuator actuator;
  actuator.set_actuator_name("servo_1");
  actuator.set_id(1);
  actuator.set_motor_type(robot::action::MotorType::MOTOR_STS3215);
  actuator.set_board_name("arm_bus");
  actuator.set_channel(1);
  actuator.set_physical_lower_limit(0.0f);
  actuator.set_physical_upper_limit(4095.0f);
  actuator.set_operational_lower_limit(973.0f);
  actuator.set_operational_upper_limit(3034.0f);
  auto* sts_config = actuator.mutable_sts3215_config();
  sts_config->set_servo_id(1);
  sts_config->set_move_speed(3000);
  // Deliberately just outside the operational range, matching a real so100
  // preset (servo_2's idle_position sits below its operational_lower_limit)
  // to exercise SetIdlePosition's bypass of the operational-limit check.
  sts_config->set_idle_position(950.0f);
  return actuator;
}

TEST(Sts3215DriverTest, InitRejectsNullChannel) {
  Sts3215Driver driver(nullptr, MakeServoActuator());

  EXPECT_EQ(driver.Init().code(), absl::StatusCode::kInvalidArgument);
}

TEST(Sts3215DriverTest, InitSeedsChannelWithConfiguredDefaultSpeed) {
  auto channel = std::make_shared<RecordingChannel>();
  Sts3215Driver driver(channel, MakeServoActuator());

  ASSERT_TRUE(driver.Init().ok());

  EXPECT_EQ(channel->set_target_calls_, 1);
  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kVelocity);
  EXPECT_FLOAT_EQ(channel->last_value_, 3000.0f);
}

TEST(Sts3215DriverTest, SetCommandsValidateInputs) {
  auto channel = std::make_shared<RecordingChannel>();
  Sts3215Driver driver(channel, MakeServoActuator());
  ASSERT_TRUE(driver.Init().ok());

  EXPECT_EQ(driver.SetSpeed(-1.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(driver.SetTorque(-1.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(driver.SetPosition(100.0f).code(), absl::StatusCode::kInvalidArgument);
  // Only the Init() speed-seed reached the channel; rejected commands did not.
  EXPECT_EQ(channel->set_target_calls_, 1);
}

TEST(Sts3215DriverTest, SetPositionPassesRawTicksThrough) {
  auto channel = std::make_shared<RecordingChannel>();
  Sts3215Driver driver(channel, MakeServoActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetPosition(2070.0f).ok());

  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kPosition);
  // Unlike TiDemoDriver, STS3215 presets already express limits in native
  // ticks, so the driver does no unit conversion.
  EXPECT_FLOAT_EQ(channel->last_value_, 2070.0f);
}

TEST(Sts3215DriverTest, SetTorquePositiveEnablesChannel) {
  auto channel = std::make_shared<RecordingChannel>();
  Sts3215Driver driver(channel, MakeServoActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetTorque(1.0f).ok());

  EXPECT_EQ(channel->enable_calls_, 1);
  EXPECT_EQ(channel->disable_calls_, 0);
}

TEST(Sts3215DriverTest, SetTorqueZeroDisablesChannel) {
  auto channel = std::make_shared<RecordingChannel>();
  Sts3215Driver driver(channel, MakeServoActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetTorque(0.0f).ok());

  EXPECT_EQ(channel->disable_calls_, 1);
  EXPECT_EQ(channel->enable_calls_, 0);
}

TEST(Sts3215DriverTest, SetIdlePositionBypassesOperationalLimits) {
  auto channel = std::make_shared<RecordingChannel>();
  Sts3215Driver driver(channel, MakeServoActuator());
  ASSERT_TRUE(driver.Init().ok());

  auto status = driver.SetIdlePosition();

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kPosition);
  EXPECT_FLOAT_EQ(channel->last_value_, 950.0f);
}

TEST(Sts3215DriverTest, SetActionSurfacesChannelFailure) {
  auto channel = std::make_shared<RecordingChannel>();
  channel->set_target_status_ = absl::Status(absl::StatusCode::kUnavailable, "no ack");
  Sts3215Driver driver(channel, MakeServoActuator());
  ASSERT_TRUE(driver.Init().ok());

  robot::action::ActionPacket packet;
  packet.set_position(2070.0f);

  EXPECT_EQ(driver.SetAction(packet).code(), absl::StatusCode::kUnavailable);
}

TEST(Sts3215DriverTest, JointCommandConvertsPositionAndRejectsUnsupportedFieldsBeforeWrites) {
  auto channel = std::make_shared<RecordingChannel>();
  Sts3215Driver driver(channel, MakeServoActuator());
  ASSERT_TRUE(driver.Init().ok());
  ActionPacket packet;
  auto* command = packet.mutable_joint_command();
  command->set_joint_name("servo_1");
  command->set_position(3.14159265358979323846);
  command->set_frame_id("base");
  packet.set_timestamp_ns(123);
  ASSERT_TRUE(driver.SetAction(packet).ok());
  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kPosition);
  EXPECT_NEAR(channel->last_value_, 2048.0f, 0.001f);
  const int writes = channel->set_target_calls_;
  const int enables = channel->enable_calls_;
  const int disables = channel->disable_calls_;
  for (double value : {0.0, -1.0, 1.0}) {
    command->set_velocity(value);
    EXPECT_EQ(driver.SetAction(packet).code(), absl::StatusCode::kUnimplemented);
    command->clear_velocity();
    command->set_effort(value);
    EXPECT_EQ(driver.SetAction(packet).code(), absl::StatusCode::kUnimplemented);
    command->clear_effort();
  }
  packet.set_normalized(true);
  EXPECT_FALSE(driver.SetAction(packet).ok());
  packet.set_normalized(false);
  command->set_joint_name("wrong_joint");
  EXPECT_FALSE(driver.SetAction(packet).ok());
  command->set_joint_name("servo_1");
  for (double value : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       std::numeric_limits<double>::max(),
                       std::numeric_limits<double>::denorm_min(),
                       1000.0}) {
    command->set_position(value);
    EXPECT_FALSE(driver.SetAction(packet).ok());
  }
  command->clear_position();
  EXPECT_FALSE(driver.SetAction(packet).ok());
  command->set_velocity(0);
  command->set_effort(0);
  EXPECT_FALSE(driver.SetAction(packet).ok());
  EXPECT_EQ(channel->set_target_calls_, writes);
  EXPECT_EQ(channel->enable_calls_, enables);
  EXPECT_EQ(channel->disable_calls_, disables);
}

}  // namespace
}  // namespace robot::action
