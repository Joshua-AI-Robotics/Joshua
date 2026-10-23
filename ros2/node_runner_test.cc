#include "ros2/node_runner.h"

#include <time.h>
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
}

class ClockObservingNode : public rclcpp::Node {
 public:
  ClockObservingNode(const std::string&, int, const config::Config&)
      : rclcpp::Node("clock_observing_node") {
    timespec before{}, after{};
    if (clock_gettime(CLOCK_MONOTONIC, &before) != 0) _exit(10);
    const double now = joshua::RobotTime();
    if (clock_gettime(CLOCK_MONOTONIC, &after) != 0) _exit(11);
    if (now < before.tv_sec || now > after.tv_sec + 1.0) _exit(12);
    // Spin one callback as well as reading from the constructor.
    timer_ = create_wall_timer(std::chrono::milliseconds(1), [] {
      if (joshua::RobotTime() <= 0) _exit(13);
      rclcpp::shutdown();
    });
  }

 private:
  rclcpp::TimerBase::SharedPtr timer_;
};

template <typename NodeT>
int RunWithClockConfig(const std::string& clock_config) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  if (!test_tmpdir || setenv("ROS_LOG_DIR", test_tmpdir, 1) != 0) _exit(14);
  std::string path = std::string(test_tmpdir) + "/clock_runner.pbtxt";
  {
    std::ofstream file(path);
    file << "general { robot_clock { " << clock_config << " } }";
    if (!file.good()) _exit(15);
  }
  std::string binary = "node_runner_test", name = "clock_test", id = "1";
  char* argv[] = {binary.data(), name.data(), id.data(), path.data(), nullptr};
  const int result = ros2_utils::RunNode<NodeT>(4, argv, "clock_test");
  if (rclcpp::ok()) _exit(16);
  return result;
}

class NodeRunnerClockTest : public testing::Test {
 protected:
  void SetUp() override {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
  }
};

TEST_F(NodeRunnerClockTest, InstallsConfiguredClockBeforeConstructionAndCallback) {
  ASSERT_EXIT(_exit(RunWithClockConfig<ClockObservingNode>("source: MONOTONIC")),
              testing::ExitedWithCode(0),
              "");
}

TEST_F(NodeRunnerClockTest, ClockFailurePreventsNodeConstruction) {
  ASSERT_EXIT(_exit(RunWithClockConfig<MustNotConstructNode>(
                  "source: PTP require_ptp: true ptp_utc_offset_seconds: 0 "
                  "ptp_device: '/nonexistent-joshua-test/ptp'")),
              testing::ExitedWithCode(1),
              "Failed to create robot clock");
}

TEST_F(NodeRunnerClockTest, EarlyLazyInitializationPreventsNodeConstruction) {
  ASSERT_EXIT(
      {
        joshua::RobotTime();
        _exit(RunWithClockConfig<MustNotConstructNode>("source: UTC"));
      },
      testing::ExitedWithCode(1),
      "already initialized");
}
}  // namespace
