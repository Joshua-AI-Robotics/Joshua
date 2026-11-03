#include "config/robot_capabilities.h"

#include <fstream>
#include <limits>
#include <sstream>

#include "config/config_utils.h"
#include "config/validation.h"
#include "google/protobuf/text_format.h"
#include "gtest/gtest.h"

namespace config {
namespace {
const google::protobuf::Value& Field(const google::protobuf::Value& v, const char* key) {
  return v.struct_value().fields().at(key);
}
Config Preset() {
  std::ifstream input("config/config_preset/so100/teleoperate.pbtxt");
  std::stringstream text;
  text << input.rdbuf();
  Config cfg;
  EXPECT_TRUE(google::protobuf::TextFormat::ParseFromString(text.str(), &cfg));
  return cfg;
}
TEST(RobotCapabilities, ReportsExactConfiguredContractAndFeedbackGap) {
  auto data = BuildRobotCapabilities(Preset());
  EXPECT_EQ(Field(data, "config_name").string_value(), "so100");
  EXPECT_EQ(Field(data, "hardware_readiness").string_value(), "unknown");
  const auto& operations = Field(data, "operations").list_value();
  ASSERT_EQ(operations.values_size(), 2);
  const auto& position = operations.values(0);
  EXPECT_EQ(position.struct_value().fields().count("verification"), 1);
  EXPECT_EQ(operations.values(1).struct_value().fields().count("verification"), 1);
  EXPECT_EQ(Field(position, "mcp_execution").string_value(), "unavailable");
  const auto& targets = Field(position, "targets").list_value();
  ASSERT_EQ(targets.values_size(), 6);
  const auto& second = targets.values(1);
  EXPECT_EQ(Field(second, "units").string_value(), "native_ticks");
  EXPECT_EQ(Field(second, "operational_lower_limit").number_value(), 858);
  EXPECT_EQ(Field(second, "operational_upper_limit").number_value(), 3154);
  EXPECT_EQ(Field(second, "idle_position").number_value(), 847);
  EXPECT_TRUE(Field(second, "feedback_sensor_ids").list_value().values().empty());
  const auto& endpoint = Field(second, "configured_ros").list_value().values(0);
  EXPECT_EQ(Field(endpoint, "message_type").string_value(), "FLOAT32");
  EXPECT_EQ(Field(endpoint, "command").string_value(), "position");
  EXPECT_EQ(Field(endpoint, "units").string_value(), "native_ticks");
  const auto& sensors = Field(operations.values(1), "targets").list_value();
  ASSERT_EQ(sensors.values_size(), 6);
  EXPECT_EQ(Field(sensors.values(0), "mcp_feedback").string_value(),
            "not_configured_for_get_feedback");
}
TEST(RobotCapabilities, IdentityComesFromConfigAndAssociationFromBoardChannel) {
  auto cfg = Preset();
  cfg.mutable_general()->set_name("renamed");
  auto* action = cfg.mutable_robot()->mutable_actions()->mutable_single_actions(0);
  action->mutable_actuator()->set_actuator_name("renamed_joint");
  action->mutable_node()->mutable_subscriptions(0)->set_topic("renamed_joint/position");
  auto* sensor = cfg.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  sensor->mutable_sts3215_encoder_config()->set_board_name("arm_bus");
  auto data = BuildRobotCapabilities(cfg);
  const auto& target =
      Field(Field(data, "operations").list_value().values(0), "targets").list_value().values(0);
  EXPECT_EQ(Field(data, "config_name").string_value(), "renamed");
  EXPECT_EQ(Field(target, "device_id").string_value(), "actuator:renamed_joint");
  ASSERT_EQ(Field(target, "feedback_sensor_ids").list_value().values_size(), 1);
  EXPECT_EQ(Field(target, "feedback_sensor_ids").list_value().values(0).string_value(),
            "sensor:sts3215_encoder_1");
}
TEST(RobotCapabilities, DuplicateActuatorIdentitiesAreGapsInsteadOfTargets) {
  auto cfg = Preset();
  auto* duplicate = cfg.mutable_robot()->mutable_actions()->mutable_single_actions(1);
  duplicate->mutable_actuator()->set_actuator_name("sts3215_servo_1");
  duplicate->mutable_node()->mutable_subscriptions(0)->set_topic("sts3215_servo_1/position");
  cfg.mutable_robot()
      ->mutable_perceptions()
      ->mutable_single_perceptions(1)
      ->mutable_node()
      ->mutable_publishers(0)
      ->set_topic("sts3215_servo_1/position");
  ASSERT_TRUE(ValidateConfig(cfg).ok());

  auto data = BuildRobotCapabilities(cfg);
  const auto& operations = Field(data, "operations").list_value();
  const auto& targets = Field(operations.values(0), "targets").list_value();
  EXPECT_EQ(targets.values_size(), 4);
  for (const auto& target : targets.values())
    EXPECT_NE(Field(target, "device_id").string_value(), "actuator:sts3215_servo_1");
  EXPECT_EQ(Field(operations.values(1), "targets").list_value().values_size(), 6);
  bool explicit_gap = false;
  for (const auto& gap : Field(data, "gaps").list_value().values()) {
    if (Field(gap, "target").string_value() == "actuator:sts3215_servo_1" &&
        Field(gap, "reason").string_value().find("ambiguous") != std::string::npos)
      explicit_gap = true;
  }
  EXPECT_TRUE(explicit_gap);
}
TEST(RobotCapabilities, DuplicateSensorIdentitiesAreGapsInsteadOfObservations) {
  auto cfg = Preset();
  cfg.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(1)->set_sensor_name(
      "sts3215_encoder_1");
  ASSERT_TRUE(ValidateConfig(cfg).ok());

  auto data = BuildRobotCapabilities(cfg);
  const auto& operations = Field(data, "operations").list_value();
  const auto& sensors = Field(operations.values(1), "targets").list_value();
  EXPECT_EQ(sensors.values_size(), 4);
  for (const auto& sensor : sensors.values())
    EXPECT_NE(Field(sensor, "device_id").string_value(), "sensor:sts3215_encoder_1");
  EXPECT_EQ(Field(operations.values(0), "targets").list_value().values_size(), 6);
  bool explicit_gap = false;
  for (const auto& gap : Field(data, "gaps").list_value().values()) {
    if (Field(gap, "target").string_value() == "sensor:sts3215_encoder_1" &&
        Field(gap, "reason").string_value().find("ambiguous") != std::string::npos)
      explicit_gap = true;
  }
  EXPECT_TRUE(explicit_gap);
}
TEST(RobotCapabilities, FeedbackAssociationOmitsAmbiguousSensorIdentity) {
  auto cfg = Preset();
  auto* sensors = cfg.mutable_robot()->mutable_perceptions();
  // Share the leader bus in its owning node so this association passes config validation.
  auto* action = cfg.mutable_robot()->mutable_actions()->mutable_single_actions(0);
  action->mutable_actuator()->set_board_name("leader_bus");
  action->mutable_node()->CopyFrom(sensors->single_perceptions(0).node());
  auto* duplicate = sensors->mutable_single_perceptions(1);
  duplicate->set_sensor_name("sts3215_encoder_1");
  // A valid sensor outside the evidenced position path still makes its ID ambiguous.
  duplicate->set_sensor_type(robot::perception::IMAGE);
  duplicate->mutable_opencv_config();
  duplicate->mutable_node()->Clear();
  duplicate->mutable_node()->set_id(10);
  duplicate->mutable_node()->set_node_type(ros2::node::CAMERA_PUBLISHER);
  ASSERT_TRUE(ValidateConfig(cfg).ok());

  auto data = BuildRobotCapabilities(cfg);
  const auto& target =
      Field(Field(data, "operations").list_value().values(0), "targets").list_value().values(0);
  EXPECT_TRUE(Field(target, "feedback_sensor_ids").list_value().values().empty());
}
TEST(RobotCapabilities, OperationalBoundsOutsideWireRangeAreExplicitGaps) {
  for (bool below_wire_range : {true, false}) {
    auto cfg = Preset();
    auto* actuator =
        cfg.mutable_robot()->mutable_actions()->mutable_single_actions(0)->mutable_actuator();
    if (below_wire_range)
      actuator->set_operational_lower_limit(-100);
    else
      actuator->set_operational_upper_limit(65536);
    ASSERT_TRUE(ValidateConfig(cfg).ok());

    auto data = BuildRobotCapabilities(cfg);
    const auto& targets =
        Field(Field(data, "operations").list_value().values(0), "targets").list_value();
    EXPECT_EQ(targets.values_size(), 5);
    for (const auto& target : targets.values())
      EXPECT_NE(Field(target, "device_id").string_value(), "actuator:sts3215_servo_1");
    bool explicit_gap = false;
    for (const auto& gap : Field(data, "gaps").list_value().values()) {
      if (Field(gap, "target").string_value() == "actuator:sts3215_servo_1" &&
          Field(gap, "reason").string_value().find("wire range") != std::string::npos)
        explicit_gap = true;
    }
    EXPECT_TRUE(explicit_gap);
  }
}
TEST(RobotCapabilities, OperationalBoundsAtWireRangeRemainEvidenced) {
  auto cfg = Preset();
  auto* actuator =
      cfg.mutable_robot()->mutable_actions()->mutable_single_actions(0)->mutable_actuator();
  actuator->set_operational_lower_limit(0);
  actuator->set_operational_upper_limit(65535);
  ASSERT_TRUE(ValidateConfig(cfg).ok());
  auto data = BuildRobotCapabilities(cfg);
  const auto& targets =
      Field(Field(data, "operations").list_value().values(0), "targets").list_value();
  ASSERT_EQ(targets.values_size(), 6);
  EXPECT_EQ(Field(targets.values(0), "operational_lower_limit").number_value(), 0);
  EXPECT_EQ(Field(targets.values(0), "operational_upper_limit").number_value(), 65535);
}
TEST(RobotCapabilities, UnsupportedPathsHaveExplicitGaps) {
  auto cfg = Preset();
  cfg.mutable_robot()
      ->mutable_actions()
      ->mutable_single_actions(0)
      ->mutable_actuator()
      ->set_motor_type(robot::action::MOTOR_STEPPER_NEMA17);
  auto data = BuildRobotCapabilities(cfg);
  EXPECT_EQ(
      Field(Field(data, "operations").list_value().values(0), "targets").list_value().values_size(),
      5);
  EXPECT_GE(Field(data, "gaps").list_value().values_size(), 5);
  auto empty = BuildRobotCapabilities(Config());
  EXPECT_TRUE(Field(Field(empty, "operations").list_value().values(0), "targets")
                  .list_value()
                  .values()
                  .empty());
}
}  // namespace
}  // namespace config

namespace config {
TEST(RobotCapabilities, DoesNotAdvertiseUnevidencedPositionWireTypes) {
  for (auto type : {ros2::data_type::JOINT_STATE, ros2::data_type::INT8}) {
    auto cfg = Preset();
    auto* sub = cfg.mutable_robot()
                    ->mutable_actions()
                    ->mutable_single_actions(0)
                    ->mutable_node()
                    ->mutable_subscriptions(0);
    sub->set_ros2_data_type(type);
    if (type == ros2::data_type::JOINT_STATE) sub->set_topic("joint_commands");
    auto data = BuildRobotCapabilities(cfg);
    const auto& target =
        Field(Field(data, "operations").list_value().values(0), "targets").list_value().values(0);
    EXPECT_EQ(Field(target, "configured_ros").list_value().values_size(), 0);
    EXPECT_EQ(Field(target, "ros_exposure").string_value(), "not_evidenced");
  }
}
}  // namespace config

namespace config {
TEST(RobotCapabilities, NonfiniteDescriptiveLimitsProduceGapsNotNumbers) {
  for (int which : {0, 1}) {
    auto cfg = Preset();
    auto* a = cfg.mutable_robot()->mutable_actions()->mutable_single_actions(0)->mutable_actuator();
    if (which == 0)
      a->mutable_sts3215_config()->set_idle_position(std::numeric_limits<float>::quiet_NaN());
    else
      a->set_physical_upper_limit(std::numeric_limits<float>::infinity());
    auto data = BuildRobotCapabilities(cfg);
    EXPECT_EQ(Field(Field(data, "operations").list_value().values(0), "targets")
                  .list_value()
                  .values_size(),
              5);
  }
}
TEST(RobotCapabilities, InvalidServoAddressesDoNotHaveEvidencedPaths) {
  for (uint32_t id : {0u, 256u, 2u}) {
    auto cfg = Preset();
    cfg.mutable_robot()->mutable_boards(1)->mutable_channels(0)->mutable_servo_bus()->set_servo_id(
        id);
    auto data = BuildRobotCapabilities(cfg);
    EXPECT_EQ(Field(Field(data, "operations").list_value().values(0), "targets")
                  .list_value()
                  .values_size(),
              0);
  }
}
}  // namespace config
