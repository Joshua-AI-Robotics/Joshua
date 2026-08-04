#include "robot/board/factory/motor_channel_validation.h"

#include "gtest/gtest.h"

namespace robot::board {
namespace {

using robot::action::MotorType;
using robot::board::DriveInterface;

TEST(ValidateMotorChannelTest, Sts3215RequiresServoBus) {
  EXPECT_TRUE(ValidateMotorChannel(MotorType::MOTOR_STS3215, DriveInterface::SERVO_BUS_UART).ok());
  EXPECT_EQ(ValidateMotorChannel(MotorType::MOTOR_STS3215, DriveInterface::STEP_DIR).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(ValidateMotorChannel(MotorType::MOTOR_STS3215, DriveInterface::PDO_JOINT).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ValidateMotorChannelTest, StepperRequiresStepDir) {
  EXPECT_TRUE(ValidateMotorChannel(MotorType::MOTOR_STEPPER_NEMA17, DriveInterface::STEP_DIR).ok());
  EXPECT_EQ(
      ValidateMotorChannel(MotorType::MOTOR_STEPPER_NEMA17, DriveInterface::SERVO_BUS_UART).code(),
      absl::StatusCode::kInvalidArgument);
}

TEST(ValidateMotorChannelTest, TiDemoJointRequiresPdoJoint) {
  EXPECT_TRUE(ValidateMotorChannel(MotorType::MOTOR_TI_DEMO, DriveInterface::PDO_JOINT).ok());
  EXPECT_EQ(ValidateMotorChannel(MotorType::MOTOR_TI_DEMO, DriveInterface::SERVO_BUS_UART).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ValidateMotorChannelTest, MockMotorAcceptsAnyRealDrive) {
  EXPECT_TRUE(ValidateMotorChannel(MotorType::MOTOR_MOCK, DriveInterface::STEP_DIR).ok());
  EXPECT_TRUE(ValidateMotorChannel(MotorType::MOTOR_MOCK, DriveInterface::PWM_DC).ok());
  EXPECT_TRUE(ValidateMotorChannel(MotorType::MOTOR_MOCK, DriveInterface::PDO_JOINT).ok());
  EXPECT_EQ(ValidateMotorChannel(MotorType::MOTOR_MOCK, DriveInterface::DRIVE_INVALID).code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(ValidateMotorChannelTest, SpikeIsUnmappedForNow) {
  EXPECT_EQ(ValidateMotorChannel(MotorType::MOTOR_SPIKE, DriveInterface::PWM_DC).code(),
            absl::StatusCode::kUnimplemented);
}

TEST(ValidateMotorChannelTest, InvalidMotorTypeIsRejected) {
  EXPECT_EQ(ValidateMotorChannel(MotorType::MOTOR_INVALID, DriveInterface::STEP_DIR).code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace robot::board
