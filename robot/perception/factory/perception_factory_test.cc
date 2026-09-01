#include "robot/perception/factory/perception_factory.h"

#include <string>

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "gtest/gtest.h"

namespace robot::perception {
namespace {

robot::perception::SinglePerception MakeBoardSensor() {
  robot::perception::SinglePerception single_perception;
  single_perception.set_sensor_name("joint_1");
  single_perception.set_id(1);
  single_perception.set_sensor_type(robot::perception::SensorType::POSITION);
  auto* config = single_perception.mutable_sts3215_encoder_config();
  config->set_board_name("mock_bus_1");
  config->set_channel(1);
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

class PerceptionFactoryTest : public ::testing::Test {
 protected:
  void TearDown() override {
    robot::board::BoardFactory::ResetForTesting();
  }
};

TEST_F(PerceptionFactoryTest, CreatesSts3215PositionSensorOverMockBoardChannel) {
  auto robot_config = MakeRobotWithMockServoBoard();

  auto sensor_or = robot::perception::PerceptionFactory::CreatePerception(MakeBoardSensor(),
                                                                          robot_config.boards());

  ASSERT_TRUE(sensor_or.ok()) << sensor_or.status();
  EXPECT_EQ((*sensor_or)->GetId(), "joint_1");
  EXPECT_TRUE((*sensor_or)->GetData().ok());
}

TEST_F(PerceptionFactoryTest, SensorSharesTheBoardInstanceWithOtherChannels) {
  auto robot_config = MakeRobotWithMockServoBoard();

  auto first = robot::board::BoardFactory::GetOrCreate(robot_config.boards(0));
  ASSERT_TRUE(first.ok()) << first.status();

  auto sensor_or = robot::perception::PerceptionFactory::CreatePerception(MakeBoardSensor(),
                                                                          robot_config.boards());
  ASSERT_TRUE(sensor_or.ok()) << sensor_or.status();

  auto second = robot::board::BoardFactory::GetOrCreate(robot_config.boards(0));
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(first->get(), second->get());
}

TEST_F(PerceptionFactoryTest, RejectsSensorWithoutConcreteDriverConfig) {
  auto robot_config = MakeRobotWithMockServoBoard();
  auto single_perception = MakeBoardSensor();
  single_perception.clear_sts3215_encoder_config();

  auto sensor_or = robot::perception::PerceptionFactory::CreatePerception(single_perception,
                                                                          robot_config.boards());

  ASSERT_FALSE(sensor_or.ok());
  EXPECT_EQ(sensor_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(PerceptionFactoryTest, RejectsUnknownBoardName) {
  auto robot_config = MakeRobotWithMockServoBoard();
  auto single_perception = MakeBoardSensor();
  single_perception.mutable_sts3215_encoder_config()->set_board_name("no_such_board");

  auto sensor_or = robot::perception::PerceptionFactory::CreatePerception(single_perception,
                                                                          robot_config.boards());

  ASSERT_FALSE(sensor_or.ok());
  EXPECT_EQ(sensor_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(PerceptionFactoryTest, RejectsUndeclaredChannel) {
  auto robot_config = MakeRobotWithMockServoBoard();
  auto single_perception = MakeBoardSensor();
  single_perception.mutable_sts3215_encoder_config()->set_channel(9);

  auto sensor_or = robot::perception::PerceptionFactory::CreatePerception(single_perception,
                                                                          robot_config.boards());

  ASSERT_FALSE(sensor_or.ok());
  EXPECT_EQ(sensor_or.status().code(), absl::StatusCode::kNotFound);
}

TEST_F(PerceptionFactoryTest, RejectsDriverConfigWithWrongSensorType) {
  auto robot_config = MakeRobotWithMockServoBoard();
  auto single_perception = MakeBoardSensor();
  single_perception.set_sensor_type(robot::perception::SensorType::IMAGE);

  auto sensor_or = robot::perception::PerceptionFactory::CreatePerception(single_perception,
                                                                          robot_config.boards());

  ASSERT_FALSE(sensor_or.ok());
  EXPECT_EQ(sensor_or.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace robot::perception
