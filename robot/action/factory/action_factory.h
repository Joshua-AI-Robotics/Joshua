#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/action_interface.h"
#include "robot/action/motors/drivers/stepper_driver.h"
#include "robot/action/motors/drivers/sts3215_driver.h"
#include "robot/action/motors/drivers/ti_demo_driver.h"
#include "robot/board/factory/board_factory.h"
#include "robot/board/factory/board_resolver.h"
#include "robot/board/factory/motor_channel_validation.h"
#include "utils/status_macros.h"

namespace robot::action {
class ActionFactory {
 public:
  // Board-layer factory (docs/BOARD_LAYER_RFC.md §6.5): every actuator resolves
  // board_name against `boards`, validates motor_type against the channel's
  // drive, and constructs a motor driver. Callers must pass config.robot().boards()
  // (see ros2/actuator_subscriber.cc).
  static absl::StatusOr<std::unique_ptr<robot::action::ActionInterface>> CreateAction(
      const robot::action::SingleAction& single_action,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    switch (single_action.action_type()) {
      case robot::action::ActionType::ACTUATOR:
        return CreateBoardActuator(single_action.actuator(), boards);
      // TODO: Add other action types here when they are implemented
      // case robot::action::ActionType::GRIPPER:
      // case robot::action::ActionType::END_EFFECTOR:
      default:
        return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid action type.");
    }
  }

  ~ActionFactory() = default;
  ActionFactory(const ActionFactory&) = delete;
  ActionFactory& operator=(const ActionFactory&) = delete;
  ActionFactory(ActionFactory&&) = default;
  ActionFactory& operator=(ActionFactory&&) = default;

 private:
  // Resolution flow per docs/BOARD_LAYER_RFC.md §6.5: board_name -> Board
  // config -> ValidateMotorChannel -> BoardFactory -> OpenChannel -> motor
  // driver. The lookup itself lives in board_resolver.h, shared with
  // PerceptionFactory, so both layers resolve a channel the same way and
  // report the same errors. Only the last step -- which driver class --
  // depends on the axis this factory owns.
  static absl::StatusOr<std::unique_ptr<robot::action::ActionInterface>> CreateBoardActuator(
      const robot::action::Actuator& actuator,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    if (actuator.actuator_type() != robot::action::ActuatorType::ACTUATOR_INVALID) {
      return absl::Status(
          absl::StatusCode::kInvalidArgument,
          "Actuator '" + actuator.actuator_name() +
              "' sets deprecated actuator_type; omit it and set motor_type + board_name + "
              "channel (docs/BOARD_LAYER_RFC.md §6.3).");
    }
    if (actuator.motor_type() == robot::action::MotorType::MOTOR_INVALID) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "Actuator '" + actuator.actuator_name() + "' has no motor_type.");
    }
    const std::string owner = "Actuator '" + actuator.actuator_name() + "'";
    ABSL_ASSIGN_OR_RETURN(auto resolved,
                          robot::board::ResolveChannelConfig(
                              boards, owner, actuator.board_name(), actuator.channel()));
    ABSL_RETURN_IF_ERROR(
        robot::board::ValidateMotorChannel(actuator.motor_type(), resolved.channel->drive()));

    ABSL_ASSIGN_OR_RETURN(auto board, robot::board::BoardFactory::GetOrCreate(*resolved.board));
    ABSL_ASSIGN_OR_RETURN(auto channel, board->OpenChannel(actuator.channel()));

    switch (actuator.motor_type()) {
      case robot::action::MotorType::MOTOR_TI_DEMO: {
        auto driver = std::make_unique<robot::action::TiDemoDriver>(channel, actuator);
        ABSL_RETURN_IF_ERROR(driver->Init());
        return driver;
      }
      case robot::action::MotorType::MOTOR_STS3215: {
        auto driver = std::make_unique<robot::action::Sts3215Driver>(channel, actuator);
        ABSL_RETURN_IF_ERROR(driver->Init());
        return driver;
      }
      case robot::action::MotorType::MOTOR_STEPPER_NEMA17: {
        auto driver = std::make_unique<robot::action::StepperDriver>(channel, actuator);
        ABSL_RETURN_IF_ERROR(driver->Init());
        return driver;
      }
      default:
        return absl::Status(absl::StatusCode::kUnimplemented,
                            "Motor type " + robot::action::MotorType_Name(actuator.motor_type()) +
                                " has no board-layer driver yet (docs/BOARD_LAYER_RFC.md §10).");
    }
  }

  ActionFactory() = default;
};
}  // namespace robot::action
