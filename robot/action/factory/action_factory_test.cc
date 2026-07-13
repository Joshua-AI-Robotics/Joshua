#include "robot/action/factory/action_factory.h"

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "gtest/gtest.h"

namespace robot::action {
namespace {

// Board-layer single action: MOTOR_TI_DEMO bound to a MOCK board's
// PDO_JOINT channel, so the resolution flow runs without hardware.
robot::action::SingleAction MakeBoardJointSingleAction() {
  robot::action::SingleAction single_action;
  single_action.set_action_type(robot::action::ActionType::ACTUATOR);

  auto* actuator = single_action.mutable_actuator();
  actuator->set_actuator_name("joint_1");
  actuator->set_id(1);
  actuator->set_motor_type(robot::action::MotorType::MOTOR_TI_DEMO);
  actuator->set_board_name("mock_board_1");
  actuator->set_channel(0);
  actuator->set_operational_lower_limit(-90.0f);
  actuator->set_operational_upper_limit(90.0f);
  return single_action;
}

config::Robot MakeRobotWithMockBoard() {
  config::Robot robot_config;
  auto* board = robot_config.add_boards();
  board->set_name("mock_board_1");
  board->set_board_type(robot::board::BoardType::MOCK);
  auto* channel = board->add_channels();
  channel->set_index(0);
  channel->set_drive(robot::board::DriveInterface::PDO_JOINT);
  return robot_config;
}

class ActionFactoryBoardPathTest : public ::testing::Test {
 protected:
  void TearDown() override {
    robot::board::BoardFactory::ResetForTesting();
  }
};

TEST_F(ActionFactoryBoardPathTest, CreatesTiDemoDriverOverMockBoardChannel) {
  auto robot_config = MakeRobotWithMockBoard();

  auto action_or = robot::action::ActionFactory::CreateAction(MakeBoardJointSingleAction(),
                                                              robot_config.boards());

  ASSERT_TRUE(action_or.ok()) << action_or.status();
  EXPECT_EQ((*action_or)->GetId(), "ti_demo_driver_joint_1");
}

TEST_F(ActionFactoryBoardPathTest, RejectsActuatorWithoutMotorType) {
  auto robot_config = MakeRobotWithMockBoard();
  auto single_action = MakeBoardJointSingleAction();
  single_action.mutable_actuator()->set_motor_type(robot::action::MotorType::MOTOR_INVALID);

  auto action_or = robot::action::ActionFactory::CreateAction(single_action, robot_config.boards());

  EXPECT_EQ(action_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(ActionFactoryBoardPathTest, ReportsUnknownBoardName) {
  auto robot_config = MakeRobotWithMockBoard();
  auto single_action = MakeBoardJointSingleAction();
  single_action.mutable_actuator()->set_board_name("no_such_board");

  auto action_or = robot::action::ActionFactory::CreateAction(single_action, robot_config.boards());

  EXPECT_EQ(action_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(ActionFactoryBoardPathTest, ReportsUndeclaredChannel) {
  auto robot_config = MakeRobotWithMockBoard();
  auto single_action = MakeBoardJointSingleAction();
  single_action.mutable_actuator()->set_channel(5);

  auto action_or = robot::action::ActionFactory::CreateAction(single_action, robot_config.boards());

  EXPECT_EQ(action_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(ActionFactoryBoardPathTest, RejectsMotorOnIncompatibleDrive) {
  auto robot_config = MakeRobotWithMockBoard();
  robot_config.mutable_boards(0)->mutable_channels(0)->set_drive(
      robot::board::DriveInterface::STEP_DIR);

  auto action_or = robot::action::ActionFactory::CreateAction(MakeBoardJointSingleAction(),
                                                              robot_config.boards());

  EXPECT_EQ(action_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(ActionFactoryBoardPathTest, LegacyEntryPointReportsMissingBoards) {
  auto action_or = robot::action::ActionFactory::CreateAction(MakeBoardJointSingleAction());

  EXPECT_EQ(action_or.status().code(), absl::StatusCode::kNotFound);
}

TEST(ActionFactoryTest, DeprecatedAm243ActuatorTypePointsAtBoardMigration) {
  robot::action::SingleAction single_action;
  single_action.set_action_type(robot::action::ActionType::ACTUATOR);
  auto* actuator = single_action.mutable_actuator();
  actuator->set_actuator_name("am243_joint_1");
  actuator->set_actuator_type(robot::action::ActuatorType::AM243_ETHERCAT_ACTUATOR);

  auto action_or = robot::action::ActionFactory::CreateAction(single_action);

  EXPECT_EQ(action_or.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(action_or.status().message().find("board"), std::string::npos);
}

}  // namespace
}  // namespace robot::action
