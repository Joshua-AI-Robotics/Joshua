#include "node_generator/runtime_observation.h"

#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "gtest/gtest.h"

namespace node_generator {
namespace {
class ObservationPreflightTest : public ::testing::Test {
 protected:
  void SetUp() override {
    char pattern[] = "/tmp/joshua-observation-test-XXXXXX";
    const auto* created = mkdtemp(pattern);
    ASSERT_NE(created, nullptr);
    directory = created;
    auto* sensor = config.mutable_robot()->mutable_perceptions()->add_single_perceptions();
    sensor->set_sensor_name("measured");
    sensor->set_sensor_type(robot::perception::POSITION);
    sensor->mutable_sts3215_encoder_config()->set_board_name("declared_only");
    sensor->mutable_sts3215_encoder_config()->set_channel(1);
    sensor->mutable_node()->set_node_type(ros2::node::POSITION_PUBLISHER);
    sensor->mutable_node()->set_id(8);
    auto* publisher = sensor->mutable_node()->add_publishers();
    publisher->set_topic("measured/position");
    publisher->set_ros2_data_type(ros2::data_type::JOINT_STATE);
  }
  void TearDown() override {
    std::filesystem::remove_all(directory);
  }
  absl::Status Check(const std::string& sensor = "measured") {
    auto result = RuntimeObservation::Prepare(config, "candidate.pbtxt", directory, sensor);
    return result.status();
  }
  config::Config config;
  std::string directory;
};

TEST_F(ObservationPreflightTest, PreparesWithoutRosOrAnyDeclaredBoard) {
  EXPECT_TRUE(Check().ok());
  EXPECT_TRUE(Check("").ok());
}
TEST_F(ObservationPreflightTest, RefusesConcurrentLeaseAndPreservesSignalHandlers) {
  struct sigaction before {
  }, after{};
  ASSERT_EQ(sigaction(SIGINT, nullptr, &before), 0);
  auto owner = RuntimeObservation::Prepare(config, "candidate.pbtxt", directory, "measured");
  ASSERT_TRUE(owner.ok());
  EXPECT_FALSE(Check().ok());
  ASSERT_EQ(sigaction(SIGINT, nullptr, &after), 0);
  EXPECT_EQ(before.sa_handler, after.sa_handler);
  EXPECT_EQ(before.sa_flags, after.sa_flags);
}
TEST_F(ObservationPreflightTest, RejectsSimulationAndUnknownOrDuplicateSensor) {
  EXPECT_FALSE(Check("missing").ok());
  auto* sensors = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions();
  *sensors->Add() = sensors->Get(0);
  EXPECT_FALSE(Check().ok());
  sensors->RemoveLast();
  config.mutable_general()->set_operation_mode(config::General::MODE_SIMULATION);
  EXPECT_FALSE(Check().ok());
}
TEST_F(ObservationPreflightTest, RejectsCommandSourceAndWrongMeasurementType) {
  auto* sensor = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  sensor->mutable_node()->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
  EXPECT_FALSE(Check().ok());
  sensor->mutable_node()->set_node_type(ros2::node::POSITION_PUBLISHER);
  sensor->mutable_node()->mutable_publishers(0)->set_ros2_data_type(ros2::data_type::FLOAT32);
  EXPECT_FALSE(Check().ok());
}
TEST_F(ObservationPreflightTest, RejectsAmbiguousConfiguredPublisherAndInvalidTopics) {
  auto* sensor = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  auto* other = config.mutable_robot()->mutable_actions()->add_single_actions()->mutable_node();
  other->set_id(9);
  other->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
  other->add_publishers()->set_topic("/measured/position");
  EXPECT_FALSE(Check().ok());
  config.mutable_robot()->mutable_actions()->clear_single_actions();
  sensor = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  for (const auto* topic : {"/", "/double//token", "/1bad", "/bad name", "/trailing/"}) {
    sensor->mutable_node()->mutable_publishers(0)->set_topic(topic);
    EXPECT_FALSE(Check().ok()) << topic;
  }
}
}  // namespace
}  // namespace node_generator
