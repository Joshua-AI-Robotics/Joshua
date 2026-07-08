#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/action_interface.h"
#include "robot/action/motors/drivers/joint_driver.h"
#include "robot/action/motors/drivers/sts3215_driver.h"
#include "robot/board/factory/board_factory.h"
#include "robot/board/factory/motor_channel_validation.h"
#include "robot/comm/factory/comm_factory.h"
#include "utils/status_macros.h"

namespace robot::action {
class ActionFactory {
 public:
  // Board-layer path (docs/BOARD_LAYER_RFC.md §6.5): actuators that leave
  // the deprecated actuator_type unset resolve board_name against `boards`,
  // validate motor_type against the channel's drive, and get a motor driver
  // over the opened BoardChannel. Actuators that still set actuator_type
  // take the legacy path until Phase 4 ports them.
  static absl::StatusOr<std::unique_ptr<robot::action::ActionInterface>> CreateAction(
      const robot::action::SingleAction& single_action,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    switch (single_action.action_type()) {
      case robot::action::ActionType::ACTUATOR: {
        const auto& actuator = single_action.actuator();
        switch (actuator.actuator_type()) {
          case robot::action::ActuatorType::ACTUATOR_INVALID:
            return CreateBoardActuator(actuator, boards);
          case robot::action::ActuatorType::STS3215_SERVO: {
            ABSL_ASSIGN_OR_RETURN(auto serial,
                                  robot::comm::CommFactory::CreateSerial(actuator.comm()));
            auto driver = std::make_unique<robot::action::Sts3215Driver>(serial, actuator);
            ABSL_RETURN_IF_ERROR(driver->Init());
            return driver;
          }
          case robot::action::ActuatorType::AM243_ETHERCAT_ACTUATOR:
            return absl::Status(
                absl::StatusCode::kInvalidArgument,
                "AM243 actuators moved to the board layer: declare a boards{} entry and set "
                "motor_type/board_name/channel instead of actuator_type "
                "(docs/BOARD_LAYER_RFC.md §10 Phase 3).");
          default:
            return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid actuator type.");
        }
      }
      // TODO: Add other action types here when they are implemented
      // case robot::action::ActionType::GRIPPER:
      // case robot::action::ActionType::END_EFFECTOR:
      default:
        return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid action type.");
    }
  }

  // Legacy entry point for callers without a boards list; board-layer
  // actuators fail with NotFound here.
  static absl::StatusOr<std::unique_ptr<robot::action::ActionInterface>> CreateAction(
      const robot::action::SingleAction& single_action) {
    static const google::protobuf::RepeatedPtrField<robot::board::Board> kNoBoards;
    return CreateAction(single_action, kNoBoards);
  }

  ~ActionFactory() = default;
  ActionFactory(const ActionFactory&) = delete;
  ActionFactory& operator=(const ActionFactory&) = delete;
  ActionFactory(ActionFactory&&) = default;
  ActionFactory& operator=(ActionFactory&&) = default;

 private:
  // Resolution flow per docs/BOARD_LAYER_RFC.md §6.5: board_name -> Board
  // config -> BoardFactory (cached instance) -> ValidateMotorChannel ->
  // OpenChannel -> motor driver.
  static absl::StatusOr<std::unique_ptr<robot::action::ActionInterface>> CreateBoardActuator(
      const robot::action::Actuator& actuator,
      const google::protobuf::RepeatedPtrField<robot::board::Board>& boards) {
    if (actuator.motor_type() == robot::action::MotorType::MOTOR_INVALID) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "Actuator '" + actuator.actuator_name() +
                              "' sets neither actuator_type (deprecated) nor motor_type.");
    }
    if (actuator.board_name().empty()) {
      return absl::Status(absl::StatusCode::kInvalidArgument,
                          "Actuator '" + actuator.actuator_name() + "' has no board_name.");
    }

    const robot::board::Board* board_config = nullptr;
    for (const auto& board : boards) {
      if (board.name() == actuator.board_name()) {
        board_config = &board;
        break;
      }
    }
    if (board_config == nullptr) {
      return absl::Status(absl::StatusCode::kNotFound,
                          "Actuator '" + actuator.actuator_name() + "' references board '" +
                              actuator.board_name() +
                              "' but no boards{} entry declares that name.");
    }

    const robot::board::Channel* channel_config = nullptr;
    for (const auto& channel : board_config->channels()) {
      if (channel.index() == actuator.channel()) {
        channel_config = &channel;
        break;
      }
    }
    if (channel_config == nullptr) {
      return absl::Status(absl::StatusCode::kNotFound,
                          "Actuator '" + actuator.actuator_name() + "' uses channel " +
                              std::to_string(actuator.channel()) + " but board '" +
                              board_config->name() + "' does not declare it.");
    }
    ABSL_RETURN_IF_ERROR(
        robot::board::ValidateMotorChannel(actuator.motor_type(), channel_config->drive()));

    ABSL_ASSIGN_OR_RETURN(auto board, robot::board::BoardFactory::GetOrCreate(*board_config));
    ABSL_ASSIGN_OR_RETURN(auto channel, board->OpenChannel(actuator.channel()));

    switch (actuator.motor_type()) {
      case robot::action::MotorType::MOTOR_GENERIC_JOINT: {
        auto driver = std::make_unique<robot::action::JointDriver>(channel, actuator);
        ABSL_RETURN_IF_ERROR(driver->Init());
        return driver;
      }
      // MOTOR_STS3215 is ported in Phase 4, MOTOR_STEPPER_NEMA17 in Phase 5;
      // MOTOR_SPIKE and MOTOR_MOCK live on the Python factory
      // (docs/BOARD_LAYER_RFC.md §10).
      default:
        return absl::Status(absl::StatusCode::kUnimplemented,
                            "Motor type " + robot::action::MotorType_Name(actuator.motor_type()) +
                                " has no board-layer driver yet (docs/BOARD_LAYER_RFC.md §10).");
    }
  }

  ActionFactory() = default;
};
}  // namespace robot::action
