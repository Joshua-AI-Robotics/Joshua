#include "ros2/node_runner.h"

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <string>

#include "gtest/gtest.h"

namespace {
class MustNotConstructNode : public rclcpp::Node {
 public:
  MustNotConstructNode(const std::string&, int, const config::Config&)
      : rclcpp::Node("unexpected_node") {
    throw std::logic_error("Node constructed before config validation");
  }
};

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
  EXPECT_EQ(ros2_utils::RunNode<MustNotConstructNode>(4, argv, "node_runner_test"), 1);
}
}  // namespace
