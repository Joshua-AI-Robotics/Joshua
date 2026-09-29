#include "config/validation.h"

#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

namespace config {
namespace {
config::Config MakeConfig() {
  config::Config config;
  auto& robot = *config.mutable_robot();
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
            node { id: 1 node_type: POSITION_PUBLISHER }
            sensor_name: "joint"
            sensor_type: POSITION
            sts3215_encoder_config { board_name: "bus" channel: 1 }
          }
        }
      )pb",
      &robot));
  return config;
}

TEST(ValidationTest, ResolvesBoardWithoutOpeningHardware) {
  EXPECT_TRUE(ValidateConfig(MakeConfig()).ok());
}
TEST(ValidationTest, RejectsMissingBoardAndChannel) {
  auto config = MakeConfig();
  auto& robot = *config.mutable_robot();
  robot.mutable_boards(0)->set_name("other");
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kNotFound);
  robot.mutable_boards(0)->set_name("bus");
  robot.mutable_boards(0)->clear_channels();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kNotFound);
}
TEST(ValidationTest, BoardSensorsSharePortOnlyWithinOneNode) {
  auto config = MakeConfig();
  auto& robot = *config.mutable_robot();
  auto sensor = robot.perceptions().single_perceptions(0);
  *robot.mutable_perceptions()->add_single_perceptions() = sensor;
  EXPECT_TRUE(ValidateConfig(config).ok());
  robot.mutable_perceptions()->mutable_single_perceptions(1)->mutable_node()->set_id(2);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}
TEST(ValidationTest, DirectAndBoardSensorsCannotOpenSamePortInDifferentNodes) {
  auto config = MakeConfig();
  auto& robot = *config.mutable_robot();
  auto* sensor = robot.mutable_perceptions()->add_single_perceptions();
  sensor->set_sensor_name("lidar");
  sensor->set_sensor_type(robot::perception::RANGE_SCAN);
  sensor->mutable_node()->set_id(2);
  sensor->mutable_node()->set_node_type(ros2::node::LIDAR_PUBLISHER);
  auto* comm = sensor->mutable_lds01_config()->mutable_comm();
  *comm = robot.boards(0).comm();
  comm->set_transport_type(robot::comm::BYTE_STREAM);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}
TEST(ValidationTest, ActuatorAndFeedbackMustShareOneProcess) {
  auto config = MakeConfig();
  auto& robot = *config.mutable_robot();
  auto* action = robot.mutable_actions()->add_single_actions();
  action->mutable_node()->set_id(2);
  action->mutable_node()->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
  action->mutable_actuator()->set_board_name("bus");
  action->mutable_actuator()->set_channel(1);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  auto* node = robot.mutable_perceptions()->mutable_single_perceptions(0)->mutable_node();
  node->set_id(2);
  node->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
  EXPECT_TRUE(ValidateConfig(config).ok());
}
TEST(ValidationTest, DoesNotMaintainASensorToPublisherAllowlist) {
  auto config = MakeConfig();
  auto& robot = *config.mutable_robot();
  robot.mutable_perceptions()->mutable_single_perceptions(0)->mutable_node()->set_node_type(
      ros2::node::CAMERA_PUBLISHER);
  // Publisher capabilities are not a shared resource-validation concern.
  EXPECT_TRUE(ValidateConfig(config).ok());
}
TEST(ValidationTest, RejectsUnspecifiedNodeType) {
  auto config = MakeConfig();
  auto& robot = *config.mutable_robot();
  robot.mutable_perceptions()->mutable_single_perceptions(0)->mutable_node()->clear_node_type();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}
TEST(ValidationTest, RejectsConflictingNodeTypesForOneProcess) {
  auto config = MakeConfig();
  auto& robot = *config.mutable_robot();
  auto sensor = robot.perceptions().single_perceptions(0);
  sensor.mutable_node()->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
  *robot.mutable_perceptions()->add_single_perceptions() = sensor;
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}
TEST(ValidationTest, DirectSensorsDoNotRequireBoards) {
  config::Config config;
  auto& robot = *config.mutable_robot();
  auto* sensor = robot.mutable_perceptions()->add_single_perceptions();
  sensor->set_sensor_name("scan");
  sensor->set_sensor_type(robot::perception::RANGE_SCAN);
  sensor->mutable_node()->set_node_type(ros2::node::LIDAR_PUBLISHER);
  auto* comm = sensor->mutable_lds01_config()->mutable_comm();
  comm->set_comm_type(robot::comm::SERIAL);
  comm->set_transport_type(robot::comm::BYTE_STREAM);
  comm->mutable_serial_config()->set_port("/test/scan");
  comm->mutable_serial_config()->set_baudrate(230400);
  EXPECT_TRUE(ValidateConfig(config).ok());
  comm->mutable_serial_config()->clear_baudrate();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}
TEST(ValidationTest, SensorsWithoutCommDoNotRequireSerialSettings) {
  config::Config config;
  auto& robot = *config.mutable_robot();
  auto* sensor = robot.mutable_perceptions()->add_single_perceptions();
  sensor->set_sensor_name("camera");
  sensor->set_sensor_type(robot::perception::IMAGE);
  sensor->mutable_node()->set_node_type(ros2::node::CAMERA_PUBLISHER);
  sensor->mutable_opencv_config()->set_id(0);
  EXPECT_TRUE(ValidateConfig(config).ok());
}
TEST(ValidationTest, ValidatesSerialSettingsOnResolvedBoards) {
  auto config = MakeConfig();
  auto& robot = *config.mutable_robot();
  robot.mutable_boards(0)->mutable_comm()->mutable_serial_config()->clear_port();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  robot.mutable_boards(0)->mutable_comm()->mutable_serial_config()->set_port("/test/bus");
  robot.mutable_boards(0)->mutable_comm()->mutable_serial_config()->clear_baudrate();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}
TEST(ValidationTest, RejectsIncompleteSensorDefinitions) {
  auto config = MakeConfig();
  auto* sensor = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  sensor->clear_sensor_name();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  sensor->set_sensor_name("joint");
  sensor->clear_sensor_type();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  sensor->set_sensor_type(robot::perception::POSITION);
  sensor->clear_sts3215_encoder_config();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST(ValidationTest, RejectsInvalidSerialTimingWithoutHardware) {
  auto config = MakeConfig();
  auto* serial = config.mutable_robot()->mutable_boards(0)->mutable_comm()->mutable_serial_config();
  serial->set_exchange_timeout_ms(0);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  serial->set_exchange_timeout_ms(250);
  serial->set_post_open_settle_ms(2000);
  EXPECT_TRUE(ValidateConfig(config).ok());
}

TEST(ValidationTest, SharedSerialPortRequiresSameTimingPolicy) {
  auto config = MakeConfig();
  auto* robot = config.mutable_robot();
  auto second_board = robot->boards(0);
  second_board.set_name("second");
  second_board.mutable_comm()->mutable_serial_config()->set_exchange_timeout_ms(100);
  *robot->add_boards() = second_board;
  auto sensor = robot->perceptions().single_perceptions(0);
  sensor.set_sensor_name("second_joint");
  sensor.mutable_sts3215_encoder_config()->set_board_name("second");
  *robot->mutable_perceptions()->add_single_perceptions() = sensor;
  EXPECT_TRUE(ValidateConfig(config).ok());  // Explicit 100 equals the legacy default.
  robot->mutable_boards(1)->mutable_comm()->mutable_serial_config()->set_post_open_settle_ms(2000);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}
TEST(ValidationTest, RejectsDriverAndMeasurementMismatchBeforeResourceResolution) {
  auto config = MakeConfig();
  auto* sensor = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  config.mutable_robot()->clear_boards();
  sensor->set_sensor_type(robot::perception::IMAGE);
  // InvalidArgument comes from the type mismatch, before board lookup (NotFound).
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  sensor->mutable_opencv_config();
  EXPECT_TRUE(ValidateConfig(config).ok());
  sensor->set_sensor_type(robot::perception::POSITION);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  sensor->mutable_lds01_config();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}
TEST(ValidationTest, RejectsLidarWithoutByteStreamBeforeOpeningHardware) {
  auto config = MakeConfig();
  auto* sensor = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  sensor->set_sensor_type(robot::perception::RANGE_SCAN);
  auto* lidar = sensor->mutable_lds01_config();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  lidar->mutable_comm()->set_transport_type(robot::comm::MESSAGE);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}
TEST(ValidationTest, RejectsLegacyTextConfig) {
  config::Config config;
  auto& robot = *config.mutable_robot();
  // Removed fields are unknown names and must fail during text parsing.
  EXPECT_FALSE(google::protobuf::TextFormat::ParseFromString(
      "perceptions { single_perceptions { perception_type: ENCODER } }", &robot));
}

config::Config MakeEthercatConfig() {
  auto config = MakeConfig();
  auto* b = config.mutable_robot()->mutable_boards(0);
  b->set_board_type(robot::board::AM243);
  b->set_protocol(robot::board::JOSHUA_WIRE_V2);
  auto* c = b->mutable_comm();
  c->set_comm_type(robot::comm::ETHERCAT);
  c->set_transport_type(robot::comm::MESSAGE_AND_CYCLIC);
  auto* ec = c->mutable_ethercat_config();
  ec->set_interface_name("never-open-this-nic");
  ec->set_slave_index(1);
  ec->set_process_data_mode(robot::comm::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR);
  auto* t = ec->mutable_timing();
  t->set_period_us(20000);
  t->set_process_timeout_us(1000);
  t->set_state_timeout_us(1000);
  t->set_operation_timeout_us(1000000);
  t->set_mailbox_step_budget_us(1000);
  t->set_scheduling_guard_us(1000);
  t->set_response_timeout_us(1000000);
  return config;
}

TEST(ValidationTest, EthercatRequiresExplicitEndpointProtocolAndFittingBudgets) {
  auto config = MakeEthercatConfig();
  EXPECT_TRUE(ValidateConfig(config).ok());
  auto* b = config.mutable_robot()->mutable_boards(0);
  b->mutable_comm()->mutable_ethercat_config()->mutable_timing()->clear_mailbox_step_budget_us();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  *b = MakeEthercatConfig().robot().boards(0);
  b->mutable_comm()->mutable_ethercat_config()->mutable_timing()->set_period_us(3000);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  *b = MakeEthercatConfig().robot().boards(0);
  b->mutable_comm()->mutable_ethercat_config()->set_slave_index(65536);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  *b = MakeEthercatConfig().robot().boards(0);
  b->clear_protocol();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  *b = MakeEthercatConfig().robot().boards(0);
  b->mutable_am243_config()->set_slave_index(1);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST(ValidationTest, EthercatRejectsDuplicateSlavesAndMixedNicPolicies) {
  auto config = MakeEthercatConfig();
  auto duplicate = config.robot().boards(0);
  duplicate.set_name("other");
  auto* b = config.mutable_robot()->add_boards();
  *b = duplicate;
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  b->mutable_comm()->mutable_ethercat_config()->set_slave_index(2);
  EXPECT_TRUE(ValidateConfig(config).ok());
  b->mutable_comm()->mutable_ethercat_config()->mutable_timing()->set_response_timeout_us(500000);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  b->mutable_comm()->set_transport_type(robot::comm::CYCLIC);
  b->clear_protocol();
  b->mutable_comm()->mutable_ethercat_config()->clear_slave_index();
  b->mutable_comm()->mutable_ethercat_config()->clear_timing();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST(ValidationTest, RetiredTiDemoConfigsAreRejectedWithoutHardware) {
  auto config = MakeEthercatConfig();
  auto* board = config.mutable_robot()->mutable_boards(0);
  board->clear_protocol();
  board->mutable_comm()->set_transport_type(robot::comm::CYCLIC);
  board->mutable_comm()->mutable_ethercat_config()->clear_timing();
  board->mutable_comm()->mutable_ethercat_config()->clear_slave_index();
  auto status = ValidateConfig(config);
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(status.message().find("retired"), std::string::npos);
  config = MakeEthercatConfig();
  auto* actuator =
      config.mutable_robot()->mutable_actions()->add_single_actions()->mutable_actuator();
  actuator->set_motor_type(robot::action::MOTOR_TI_DEMO);
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  actuator->set_motor_type(robot::action::MOTOR_STEPPER_NEMA17);
  actuator->mutable_am243_ethercat_config();
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
}

TEST(ValidationTest, EthercatNicCannotBeSplitAcrossNodeProcesses) {
  auto config = MakeEthercatConfig();
  auto sensor = config.robot().perceptions().single_perceptions(0);
  sensor.mutable_node()->set_id(2);
  *config.mutable_robot()->mutable_perceptions()->add_single_perceptions() = sensor;
  EXPECT_EQ(ValidateConfig(config).code(), absl::StatusCode::kInvalidArgument);
  config.mutable_robot()
      ->mutable_perceptions()
      ->mutable_single_perceptions(1)
      ->mutable_node()
      ->set_id(1);
  EXPECT_TRUE(ValidateConfig(config).ok());
}
}  // namespace
}  // namespace config
