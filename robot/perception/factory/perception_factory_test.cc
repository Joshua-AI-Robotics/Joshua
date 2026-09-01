#include "robot/perception/factory/perception_factory.h"

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "gtest/gtest.h"

namespace robot::perception {
namespace {

// Board-layer perception: an STS3215 encoder bound to a MOCK board's
// SERVO_BUS_UART channel, so the resolution flow runs without hardware --
// the perception twin of ActionFactoryBoardPathTest.
robot::perception::SinglePerception MakeEncoderPerception() {
  robot::perception::SinglePerception single_perception;
  single_perception.set_perception_type(robot::perception::PerceptionType::ENCODER);

  auto* encoder = single_perception.mutable_encoder();
  encoder->set_encoder_name("sts3215_encoder_1");
  encoder->set_id(1);
  encoder->set_encoder_type(robot::perception::EncoderType::STS3215_ENCODER);
  encoder->set_board_name("mock_bus_1");
  encoder->set_channel(1);
  encoder->set_operational_lower_limit(1147.0f);
  encoder->set_operational_upper_limit(3154.0f);
  return single_perception;
}

config::Robot MakeRobotWithMockServoBoard() {
  config::Robot robot_config;
  auto* board = robot_config.add_boards();
  board->set_name("mock_bus_1");
  board->set_board_type(robot::board::BoardType::MOCK);
  auto* channel = board->add_channels();
  channel->set_index(1);
  channel->set_drive(robot::board::DriveInterface::SERVO_BUS_UART);
  channel->mutable_servo_bus()->set_servo_id(1);
  return robot_config;
}

class PerceptionFactoryBoardPathTest : public ::testing::Test {
 protected:
  void TearDown() override {
    robot::board::BoardFactory::ResetForTesting();
  }
};

TEST_F(PerceptionFactoryBoardPathTest, CreatesEncoderOverMockBoardChannel) {
  auto robot_config = MakeRobotWithMockServoBoard();

  auto perception_or = robot::perception::PerceptionFactory::CreatePerception(
      MakeEncoderPerception(), robot_config.boards());

  ASSERT_TRUE(perception_or.ok()) << perception_or.status();
  EXPECT_EQ((*perception_or)->GetId(), "sts3215_encoder_1");
  EXPECT_TRUE((*perception_or)->GetData().ok());
}

// An encoder and an actuator naming the same board share one instance and
// therefore one bus mutex -- the whole point of routing perception through
// the board layer (docs/BOARD_LAYER_RFC.md §5.3).
TEST_F(PerceptionFactoryBoardPathTest, EncoderSharesTheBoardInstanceWithOtherChannels) {
  auto robot_config = MakeRobotWithMockServoBoard();

  auto first = robot::board::BoardFactory::GetOrCreate(robot_config.boards(0));
  ASSERT_TRUE(first.ok()) << first.status();

  auto perception_or = robot::perception::PerceptionFactory::CreatePerception(
      MakeEncoderPerception(), robot_config.boards());
  ASSERT_TRUE(perception_or.ok()) << perception_or.status();

  auto second = robot::board::BoardFactory::GetOrCreate(robot_config.boards(0));
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(first->get(), second->get());
}

TEST_F(PerceptionFactoryBoardPathTest, RejectsDeprecatedInlineComm) {
  auto robot_config = MakeRobotWithMockServoBoard();
  auto single_perception = MakeEncoderPerception();
  single_perception.mutable_encoder()->mutable_comm()->set_comm_type(robot::comm::CommType::SERIAL);

  auto perception_or = robot::perception::PerceptionFactory::CreatePerception(
      single_perception, robot_config.boards());

  ASSERT_FALSE(perception_or.ok());
  EXPECT_EQ(perception_or.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(perception_or.status().message().find("deprecated comm"), std::string::npos)
      << perception_or.status().message();
}

TEST_F(PerceptionFactoryBoardPathTest, RejectsMissingBoardName) {
  auto robot_config = MakeRobotWithMockServoBoard();
  auto single_perception = MakeEncoderPerception();
  single_perception.mutable_encoder()->clear_board_name();

  auto perception_or = robot::perception::PerceptionFactory::CreatePerception(
      single_perception, robot_config.boards());

  ASSERT_FALSE(perception_or.ok());
  EXPECT_EQ(perception_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(PerceptionFactoryBoardPathTest, RejectsUnknownBoardName) {
  auto robot_config = MakeRobotWithMockServoBoard();
  auto single_perception = MakeEncoderPerception();
  single_perception.mutable_encoder()->set_board_name("no_such_board");

  auto perception_or = robot::perception::PerceptionFactory::CreatePerception(
      single_perception, robot_config.boards());

  ASSERT_FALSE(perception_or.ok());
  EXPECT_EQ(perception_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(PerceptionFactoryBoardPathTest, RejectsUndeclaredChannel) {
  auto robot_config = MakeRobotWithMockServoBoard();
  auto single_perception = MakeEncoderPerception();
  single_perception.mutable_encoder()->set_channel(9);

  auto perception_or = robot::perception::PerceptionFactory::CreatePerception(
      single_perception, robot_config.boards());

  ASSERT_FALSE(perception_or.ok());
  EXPECT_EQ(perception_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(PerceptionFactoryBoardPathTest, RejectsChannelWhoseDriveCannotReportPosition) {
  config::Robot robot_config;
  auto* board = robot_config.add_boards();
  board->set_name("mock_bus_1");
  board->set_board_type(robot::board::BoardType::MOCK);
  auto* channel = board->add_channels();
  channel->set_index(1);
  channel->set_drive(robot::board::DriveInterface::STEP_DIR);

  auto perception_or = robot::perception::PerceptionFactory::CreatePerception(
      MakeEncoderPerception(), robot_config.boards());

  ASSERT_FALSE(perception_or.ok());
  EXPECT_EQ(perception_or.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace robot::perception
