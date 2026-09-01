#include "robot/board/factory/sensor_channel_validation.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace robot::board {
namespace {

TEST(SensorChannelValidationTest, Sts3215EncoderAcceptsServoBusChannel) {
  EXPECT_TRUE(ValidateSensorChannel(robot::perception::EncoderType::STS3215_ENCODER,
                                    robot::board::DriveInterface::SERVO_BUS_UART)
                  .ok());
}

TEST(SensorChannelValidationTest, Sts3215EncoderRejectsStepDirChannel) {
  const auto status = ValidateSensorChannel(robot::perception::EncoderType::STS3215_ENCODER,
                                            robot::board::DriveInterface::STEP_DIR);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("SERVO_BUS_UART"), std::string::npos) << status.message();
  EXPECT_NE(status.message().find("STEP_DIR"), std::string::npos) << status.message();
}

TEST(SensorChannelValidationTest, RejectsInvalidEncoderType) {
  EXPECT_EQ(ValidateSensorChannel(robot::perception::EncoderType::ENCODER_INVLAID,
                                  robot::board::DriveInterface::SERVO_BUS_UART)
                .code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace robot::board
