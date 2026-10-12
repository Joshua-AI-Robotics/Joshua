#include "ros2/node_runner.h"

#include <unistd.h>

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
  EXPECT_FALSE(rclcpp::ok());
}

class NetworkObservingNode : public rclcpp::Node {
 public:
  NetworkObservingNode(const std::string&, int, const config::Config&)
      : rclcpp::Node("network_observing_node") {
    if (get_node_base_interface()->get_context()->get_domain_id() != 17) _exit(10);
    timer_ = create_wall_timer(std::chrono::milliseconds(1), [] { rclcpp::shutdown(); });
  }

 private:
  rclcpp::TimerBase::SharedPtr timer_;
};

TEST(NodeRunnerTest, AppliesDomainBeforeInitializingRos) {
  GTEST_FLAG_SET(death_test_style, "threadsafe");
  ASSERT_EXIT(([] {
                setenv("ROS_DOMAIN_ID", "18", 1);
                setenv("ROS_LOG_DIR", std::getenv("TEST_TMPDIR"), 1);
                std::string path = std::string(std::getenv("TEST_TMPDIR")) + "/network.pbtxt";
                {
                  std::ofstream file(path);
                  file << "general { ros2_network { domain_id: 17 } }";
                }
                std::string binary = "test", name = "test", id = "1";
                char* args[] = {binary.data(), name.data(), id.data(), path.data()};
                const int result = ros2_utils::RunNode<NetworkObservingNode>(4, args, "test");
                if (rclcpp::ok()) _exit(11);
                _exit(result);
              }()),
              testing::ExitedWithCode(0),
              "");
}
}  // namespace
