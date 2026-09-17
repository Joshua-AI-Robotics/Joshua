#include "robot/perception/factory/perception_factory.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "gtest/gtest.h"
#include "robot/board/factory/board_factory.h"
#include "robot/comm/factory/comm_factory.h"

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
    robot::comm::CommFactory::SetCommTransportFactoryForTesting(nullptr);
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
  EXPECT_EQ(sensor_or.status().message(), "Sensor 'joint_1' has no sensor_config.");
}

TEST_F(PerceptionFactoryTest, RejectsConcreteDriverConfigWithoutSensorType) {
  auto robot_config = MakeRobotWithMockServoBoard();
  auto single_perception = MakeBoardSensor();
  single_perception.clear_sensor_type();

  auto sensor_or = PerceptionFactory::CreatePerception(single_perception, robot_config.boards());

  ASSERT_FALSE(sensor_or.ok());
  EXPECT_EQ(sensor_or.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(sensor_or.status().message(), "Sensor 'joint_1' has no sensor_type.");
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

class RecordingStream : public robot::comm::ByteStream {
 public:
  absl::Status Open() override {
    ++opens;
    return open_status;
  }
  absl::Status Write(const std::vector<uint8_t>& data) override {
    writes.push_back(data);
    return absl::OkStatus();
  }
  absl::StatusOr<std::vector<uint8_t>> Read(size_t size) override {
    if (size == 1) return std::vector<uint8_t>{reads++ == 0 ? uint8_t{0xfa} : uint8_t{0xa0}};
    // Remaining bytes of a scan with one valid 42-byte block and a 1 m first return.
    std::vector<uint8_t> bytes(size, 0);
    bytes[4] = 0xe8;
    bytes[5] = 0x03;
    return bytes;
  }
  int opens = 0;
  int reads = 0;
  absl::Status open_status = absl::OkStatus();
  std::vector<std::vector<uint8_t>> writes;
};

SinglePerception MakeLidar() {
  SinglePerception sensor;
  sensor.set_sensor_name("front_scan");
  sensor.set_sensor_type(RANGE_SCAN);
  auto* comm = sensor.mutable_lds01_config()->mutable_comm();
  comm->set_comm_type(robot::comm::SERIAL);
  comm->set_transport_type(robot::comm::BYTE_STREAM);
  comm->mutable_serial_config()->set_port("/test/lidar");
  comm->mutable_serial_config()->set_baudrate(230400);
  return sensor;
}

TEST_F(PerceptionFactoryTest, CreatesCameraWithConfiguredNameWithoutOpeningHardware) {
  SinglePerception sensor;
  sensor.set_sensor_name("front_camera");
  sensor.set_sensor_type(IMAGE);
  sensor.mutable_opencv_config()->set_id(7);
  config::Robot robot;
  auto result = PerceptionFactory::CreatePerception(sensor, robot.boards());
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ((*result)->GetId(), "front_camera");
}

TEST_F(PerceptionFactoryTest, CreatesLidarThroughByteStreamAndPreservesPacketName) {
  auto stream = std::make_shared<RecordingStream>();
  robot::comm::CommFactory::SetCommTransportFactoryForTesting(
      [stream](const robot::comm::Comm& comm) -> absl::StatusOr<robot::comm::CommTransport> {
        EXPECT_EQ(comm.serial_config().port(), "/test/lidar");
        return robot::comm::CommTransport{
            std::static_pointer_cast<robot::comm::ByteStream>(stream)};
      });
  config::Robot robot;
  auto result = PerceptionFactory::CreatePerception(MakeLidar(), robot.boards());
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ((*result)->GetId(), "front_scan");
  EXPECT_EQ(stream->opens, 1);
  ASSERT_EQ(stream->writes.size(), 1);
  EXPECT_EQ(stream->writes.front(), std::vector<uint8_t>{'b'});
  auto packet = (*result)->GetData();
  ASSERT_TRUE(packet.ok()) << packet.status();
  EXPECT_EQ(packet->perception_id(), "front_scan");
  EXPECT_FLOAT_EQ(packet->point_cloud().x(0), 1.0f);
  EXPECT_TRUE((*result)->Teardown().ok());
  EXPECT_EQ(stream->writes.back(), std::vector<uint8_t>{'e'});
}

TEST_F(PerceptionFactoryTest, PropagatesLidarInitializationFailure) {
  auto stream = std::make_shared<RecordingStream>();
  stream->open_status = absl::UnavailableError("disconnected");
  robot::comm::CommFactory::SetCommTransportFactoryForTesting(
      [stream](const robot::comm::Comm&) -> absl::StatusOr<robot::comm::CommTransport> {
        return robot::comm::CommTransport{
            std::static_pointer_cast<robot::comm::ByteStream>(stream)};
      });
  config::Robot robot;
  auto result = PerceptionFactory::CreatePerception(MakeLidar(), robot.boards());
  ASSERT_FALSE(result.ok());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kUnavailable);
  EXPECT_TRUE(stream->writes.empty());
}

TEST_F(PerceptionFactoryTest, RejectsWrongDirectSensorTypesBeforeOpeningComm) {
  bool called = false;
  robot::comm::CommFactory::SetCommTransportFactoryForTesting(
      [&called](const robot::comm::Comm&) -> absl::StatusOr<robot::comm::CommTransport> {
        called = true;
        return absl::InternalError("should not open hardware");
      });
  config::Robot robot;
  auto sensor = MakeLidar();
  sensor.set_sensor_type(POSITION);
  EXPECT_EQ(PerceptionFactory::CreatePerception(sensor, robot.boards()).status().code(),
            absl::StatusCode::kInvalidArgument);
  sensor.mutable_opencv_config();
  EXPECT_EQ(PerceptionFactory::CreatePerception(sensor, robot.boards()).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_FALSE(called);
}

TEST_F(PerceptionFactoryTest, RejectsLidarWithoutByteStreamConfig) {
  config::Robot robot;
  auto sensor = MakeLidar();
  sensor.mutable_lds01_config()->mutable_comm()->set_transport_type(robot::comm::MESSAGE);
  EXPECT_EQ(PerceptionFactory::CreatePerception(sensor, robot.boards()).status().code(),
            absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace robot::perception
