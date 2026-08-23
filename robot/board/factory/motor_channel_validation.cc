#include "robot/board/factory/motor_channel_validation.h"

#include "absl/strings/str_cat.h"

namespace robot::board {

namespace {

absl::Status RequireDrive(robot::action::MotorType motor_type,
                          robot::board::DriveInterface actual,
                          robot::board::DriveInterface required) {
  if (actual == required) {
    return absl::OkStatus();
  }
  return absl::InvalidArgumentError(absl::StrCat("Motor ",
                                                 robot::action::MotorType_Name(motor_type),
                                                 " requires a ",
                                                 robot::board::DriveInterface_Name(required),
                                                 " channel, but the channel's drive is ",
                                                 robot::board::DriveInterface_Name(actual),
                                                 "."));
}

}  // namespace

absl::Status ValidateMotorChannel(robot::action::MotorType motor_type,
                                  robot::board::DriveInterface drive) {
  switch (motor_type) {
    case robot::action::MotorType::MOTOR_STS3215:
      return RequireDrive(motor_type, drive, robot::board::DriveInterface::SERVO_BUS_UART);
    case robot::action::MotorType::MOTOR_STEPPER_NEMA17:
      return RequireDrive(motor_type, drive, robot::board::DriveInterface::STEP_DIR);
    case robot::action::MotorType::MOTOR_TI_DEMO:
      return RequireDrive(motor_type, drive, robot::board::DriveInterface::PDO_JOINT);
    case robot::action::MotorType::MOTOR_INVALID:
    default:
      return absl::InvalidArgumentError("Actuator has an invalid motor_type.");
  }
}

}  // namespace robot::board
