#include "robot/board/factory/sensor_channel_validation.h"

#include "absl/strings/str_cat.h"

namespace robot::board {

namespace {

absl::Status RequireDrive(robot::perception::EncoderType encoder_type,
                          robot::board::DriveInterface actual,
                          robot::board::DriveInterface required) {
  if (actual == required) {
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(absl::StrCat("Encoder ",
                                                 robot::perception::EncoderType_Name(encoder_type),
                                                 " requires a ",
                                                 robot::board::DriveInterface_Name(required),
                                                 " channel, but the channel's drive is ",
                                                 robot::board::DriveInterface_Name(actual),
                                                 "."));
}

}  // namespace

absl::Status ValidateSensorChannel(robot::perception::EncoderType encoder_type,
                                   robot::board::DriveInterface drive) {
  switch (encoder_type) {
    case robot::perception::EncoderType::STS3215_ENCODER:
      return RequireDrive(encoder_type, drive, robot::board::DriveInterface::SERVO_BUS_UART);
    case robot::perception::EncoderType::ENCODER_INVLAID:
    default:
      return absl::InvalidArgumentError("Encoder has an invalid encoder_type.");
  }
}

}  // namespace robot::board
