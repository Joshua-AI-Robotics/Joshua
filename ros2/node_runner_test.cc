#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

#include "config/proto/config.pb.h"
#include "gtest/gtest.h"
#include "rclcpp/rclcpp.hpp"

namespace {
bool constructed = false;
}

namespace ros2_utils {
int RunNode(int argc, char* argv[]);
std::shared_ptr<rclcpp::Node> CreateValidatedNode(const std::string&, int, const config::Config&);

std::shared_ptr<rclcpp::Node> CreateNode(const std::string&, int, const config::Config&) {
  constructed = true;
  throw std::logic_error("Node constructed before config validation");
}
}  // namespace ros2_utils

namespace {
TEST(NodeRunnerTest, RejectsInvalidConfigBeforeConstructingNode) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  ASSERT_NE(test_tmpdir, nullptr);
  ASSERT_EQ(setenv("ROS_LOG_DIR", test_tmpdir, 1), 0);
  std::string config_path = std::string(test_tmpdir) + "/invalid_config.pbtxt";
  {
    std::ofstream file(config_path);
    ASSERT_TRUE(file.is_open());
    // Valid protobuf syntax, but no concrete sensor config.
    file << R"pb(robot {
                   perceptions {
                     single_perceptions {
                       node { id: 1 node_type: POSITION_PUBLISHER }
                       sensor_name: "joint"
                       sensor_type: POSITION
                     }
                   }
                 })pb";
  }
  std::string binary = "node_runner_test";
  std::string node_name = "test_node";
  std::string node_id = "1";
  char* argv[] = {binary.data(), node_name.data(), node_id.data(), config_path.data(), nullptr};
  constructed = false;
  EXPECT_EQ(ros2_utils::RunNode(4, argv), 1);
  EXPECT_FALSE(constructed);
}
TEST(NodeRunnerTest, SharedConstructionRejectsInvalidConfigBeforeFactory) {
  config::Config config;
  auto* sensor = config.mutable_robot()->mutable_perceptions()->add_single_perceptions();
  sensor->set_sensor_name("joint");
  sensor->set_sensor_type(robot::perception::POSITION);
  constructed = false;
  EXPECT_THROW(ros2_utils::CreateValidatedNode("test", 1, config), std::invalid_argument);
  EXPECT_FALSE(constructed);
}
}  // namespace
