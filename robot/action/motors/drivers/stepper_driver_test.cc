#include "robot/action/motors/drivers/stepper_driver.h"

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

robot::action::Actuator MakeStepperActuator() {
  robot::action::Actuator actuator;
  actuator.set_actuator_name("stepper_1");
  actuator.set_id(1);
  actuator.set_motor_type(robot::action::MotorType::MOTOR_STEPPER_NEMA17);
  actuator.set_board_name("stepper_bus");
  actuator.set_channel(0);
  actuator.set_physical_lower_limit(-180.0f);
  actuator.set_physical_upper_limit(180.0f);
  actuator.set_operational_lower_limit(-90.0f);
  actuator.set_operational_upper_limit(90.0f);
  auto* stepper_config = actuator.mutable_stepper_config();
  // 1.8-degree/step at 1/16 microstepping: 200 * 16 / 360.
  stepper_config->set_steps_per_degree(200.0f * 16.0f / 360.0f);
  stepper_config->set_gear_ratio(1.0f);
  stepper_config->set_idle_position(-100.0f);  // Deliberately outside operational range.
  return actuator;
}

TEST(StepperDriverTest, InitRejectsNullChannel) {
  StepperDriver driver(nullptr, MakeStepperActuator());

  EXPECT_EQ(driver.Init().code(), absl::StatusCode::kInvalidArgument);
}

TEST(StepperDriverTest, SetCommandsValidateInputs) {
  auto channel = std::make_shared<RecordingChannel>();
  StepperDriver driver(channel, MakeStepperActuator());
  ASSERT_TRUE(driver.Init().ok());

  EXPECT_EQ(driver.SetSpeed(-1.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(driver.SetTorque(-1.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(driver.SetPosition(200.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(channel->set_target_calls_, 0);
}

TEST(StepperDriverTest, SetPositionConvertsDegreesToSteps) {
  auto channel = std::make_shared<RecordingChannel>();
  StepperDriver driver(channel, MakeStepperActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetPosition(45.0f).ok());

  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kPosition);
  // 45 degrees * (200*16/360) steps/degree * 1.0 gear_ratio = 400 steps.
  EXPECT_FLOAT_EQ(channel->last_value_, 400.0f);
}

TEST(StepperDriverTest, SetSpeedConvertsDegreesToSteps) {
  auto channel = std::make_shared<RecordingChannel>();
  StepperDriver driver(channel, MakeStepperActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetSpeed(9.0f).ok());

  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kVelocity);
  // 9 degrees/s * (200*16/360) steps/degree = 80 steps/s.
  EXPECT_FLOAT_EQ(channel->last_value_, 80.0f);
}

TEST(StepperDriverTest, SetTorquePositiveEnablesChannel) {
  auto channel = std::make_shared<RecordingChannel>();
  StepperDriver driver(channel, MakeStepperActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetTorque(1.0f).ok());

  EXPECT_EQ(channel->enable_calls_, 1);
  EXPECT_EQ(channel->disable_calls_, 0);
}

TEST(StepperDriverTest, SetTorqueZeroDisablesChannel) {
  auto channel = std::make_shared<RecordingChannel>();
  StepperDriver driver(channel, MakeStepperActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.SetTorque(0.0f).ok());

  EXPECT_EQ(channel->disable_calls_, 1);
  EXPECT_EQ(channel->enable_calls_, 0);
}

TEST(StepperDriverTest, SetIdlePositionBypassesOperationalLimits) {
  auto channel = std::make_shared<RecordingChannel>();
  StepperDriver driver(channel, MakeStepperActuator());
  ASSERT_TRUE(driver.Init().ok());

  auto status = driver.SetIdlePosition();

  ASSERT_TRUE(status.ok()) << status;
  EXPECT_EQ(channel->last_mode_, robot::board::TargetMode::kPosition);
  // -100 degrees * (200*16/360) steps/degree = -888.888... steps.
  EXPECT_NEAR(channel->last_value_, -888.888f, 0.01f);
}

TEST(StepperDriverTest, SetActionSurfacesChannelFailure) {
  auto channel = std::make_shared<RecordingChannel>();
  channel->set_target_status_ = absl::Status(absl::StatusCode::kUnavailable, "no ack");
  StepperDriver driver(channel, MakeStepperActuator());
  ASSERT_TRUE(driver.Init().ok());

  robot::action::ActionPacket packet;
  packet.set_position(10.0f);

  EXPECT_EQ(driver.SetAction(packet).code(), absl::StatusCode::kUnavailable);
}

TEST(StepperDriverTest, TeardownSetsIdleThenDisables) {
  auto channel = std::make_shared<RecordingChannel>();
  StepperDriver driver(channel, MakeStepperActuator());
  ASSERT_TRUE(driver.Init().ok());

  ASSERT_TRUE(driver.Teardown().ok());

  EXPECT_EQ(channel->set_target_calls_, 1);
  EXPECT_EQ(channel->disable_calls_, 1);
}

}  // namespace
}  // namespace robot::action
