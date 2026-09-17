#include "config/perception_validation.h"

#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

namespace config::config_util {
namespace {
Robot MakeRobot() {
  Robot robot;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(
      R"pb(
        boards {
          name: "bus"
          board_type: FEETECH_BUS
          comm {
            comm_type: SERIAL
            transport_type: MESSAGE
            serial_config { port: "/test/bus" baudrate: 1000000 }
          }
          channels {
            index: 1
            drive: SERVO_BUS_UART
            servo_bus { servo_id: 1 }
          }
        }
        perceptions {
          single_perceptions {
            node { id: 1 node_type: ENCODER_PUBLISHER }
            sensor_name: "joint"
            sensor_type: POSITION
            sts3215_encoder_config { board_name: "bus" channel: 1 }
          }
        }
      )pb",
      &robot));
  return robot;
}

TEST(PerceptionValidationTest, ResolvesBoardWithoutOpeningHardware) {
  EXPECT_TRUE(ValidatePerceptions(MakeRobot()).ok());
}
TEST(PerceptionValidationTest, RejectsMissingBoardAndChannel) {
  auto robot = MakeRobot();
  robot.mutable_boards(0)->set_name("other");
  EXPECT_EQ(ValidatePerceptions(robot).code(), absl::StatusCode::kNotFound);
  robot.mutable_boards(0)->set_name("bus");
  robot.mutable_boards(0)->clear_channels();
  EXPECT_EQ(ValidatePerceptions(robot).code(), absl::StatusCode::kNotFound);
}
TEST(PerceptionValidationTest, BoardSensorsSharePortOnlyWithinOneNode) {
  auto robot = MakeRobot();
  auto sensor = robot.perceptions().single_perceptions(0);
  *robot.mutable_perceptions()->add_single_perceptions() = sensor;
  EXPECT_TRUE(ValidatePerceptions(robot).ok());
  robot.mutable_perceptions()->mutable_single_perceptions(1)->mutable_node()->set_id(2);
  EXPECT_EQ(ValidatePerceptions(robot).code(), absl::StatusCode::kInvalidArgument);
}
TEST(PerceptionValidationTest, DirectAndBoardSensorsCannotOpenSamePortInDifferentNodes) {
  auto robot = MakeRobot();
  auto* sensor = robot.mutable_perceptions()->add_single_perceptions();
  sensor->set_sensor_name("lidar");
  sensor->set_sensor_type(robot::perception::RANGE_SCAN);
  sensor->mutable_node()->set_id(2);
  sensor->mutable_node()->set_node_type(ros2::node::LIDAR_PUBLISHER);
  auto* comm = sensor->mutable_lds01_config()->mutable_comm();
  *comm = robot.boards(0).comm();
  comm->set_transport_type(robot::comm::BYTE_STREAM);
  EXPECT_EQ(ValidatePerceptions(robot).code(), absl::StatusCode::kInvalidArgument);
}
TEST(PerceptionValidationTest, ActuatorAndFeedbackMustShareOneProcess) {
  auto robot = MakeRobot();
  auto* action = robot.mutable_actions()->add_single_actions();
  action->mutable_node()->set_id(2);
  action->mutable_node()->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
  action->mutable_actuator()->set_board_name("bus");
  action->mutable_actuator()->set_channel(1);
  EXPECT_EQ(ValidatePerceptions(robot).code(), absl::StatusCode::kInvalidArgument);
  auto* node = robot.mutable_perceptions()->mutable_single_perceptions(0)->mutable_node();
  node->set_id(2);
  node->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
  EXPECT_TRUE(ValidatePerceptions(robot).ok());
}
TEST(PerceptionValidationTest, RejectsPublisherThatWouldIgnoreSensor) {
  auto robot = MakeRobot();
  robot.mutable_perceptions()->mutable_single_perceptions(0)->mutable_node()->set_node_type(
      ros2::node::CAMERA_PUBLISHER);
  EXPECT_EQ(ValidatePerceptions(robot).code(), absl::StatusCode::kInvalidArgument);
}
TEST(PerceptionValidationTest, RejectsLegacyTextConfig) {
  Robot robot;
  // TextFormat skips reserved names; semantic validation must reject the
  // resulting entry instead of treating it as an empty/default sensor.
  ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(
      "perceptions { single_perceptions { perception_type: ENCODER } }", &robot));
  EXPECT_EQ(ValidatePerceptions(robot).code(), absl::StatusCode::kInvalidArgument);
}
}  // namespace
}  // namespace config::config_util
