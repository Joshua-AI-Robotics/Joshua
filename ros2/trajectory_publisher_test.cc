#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "config/proto/config.pb.h"
#include "gtest/gtest.h"
#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/float32.hpp"

namespace ros2_utils {
std::shared_ptr<rclcpp::Node> CreateValidatedNode(const std::string&, int, const config::Config&);
}  // namespace ros2_utils

namespace {
using namespace std::chrono_literals;

class TrajectoryPublisherTest : public testing::Test {
 protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
  }
  void TearDown() override {
    executor_.reset();
    rclcpp::shutdown();
  }
  config::Config Config() {
    config::Config config;
    auto* entry = config.mutable_robot()->mutable_trajectories()->add_single_trajectories();
    entry->mutable_node()->set_id(7);
    entry->mutable_node()->set_node_type(ros2::node::TRAJECTORY_PUBLISHER);
    entry->mutable_node()->mutable_qos_setting()->set_depth(10);
    // Deliberately out of order, with equal timestamps whose order must survive.
    for (const auto& [seconds, value] :
         std::vector<std::pair<double, float>>{{0.1, 3}, {0, 1}, {0, 2}}) {
      auto* waypoint = entry->mutable_trajectory()->add_waypoints();
      waypoint->set_timestamp_sec(seconds);
      waypoint->set_topic("/trajectory_test/a");
      waypoint->mutable_action()->set_position(value);
    }
    auto* gate = entry->mutable_trajectory()->add_waypoints();
    gate->set_timestamp_sec(0.1);
    gate->set_topic("/trajectory_test/b");
    gate->mutable_action()->set_speed(4);
    return config;
  }
  void SpinUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!done() && std::chrono::steady_clock::now() < deadline) {
      executor_->spin_some();
      std::this_thread::sleep_for(2ms);
    }
  }
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

TEST_F(TrajectoryPublisherTest, WaitsForAllTopicsThenSortsAndRepeats) {
  auto publisher = ros2_utils::CreateValidatedNode("trajectory_test", 7, Config());
  auto listener = std::make_shared<rclcpp::Node>("trajectory_listener");
  std::vector<float> values;
  auto first = listener->create_subscription<std_msgs::msg::Float32>(
      "/trajectory_test/a", 10, [&](std_msgs::msg::Float32::ConstSharedPtr msg) {
        values.push_back(msg->data);
      });
  executor_->add_node(publisher);
  executor_->add_node(listener);
  SpinUntil([] { return false; }, 1200ms);
  EXPECT_TRUE(values.empty());
  int gate_messages = 0;
  auto second = listener->create_subscription<std_msgs::msg::Float32>(
      "/trajectory_test/b", 10, [&](std_msgs::msg::Float32::ConstSharedPtr msg) {
        EXPECT_FLOAT_EQ(msg->data, 4);
        ++gate_messages;
      });
  SpinUntil([&] { return values.size() >= 6 && gate_messages >= 2; }, 5000ms);
  ASSERT_GE(values.size(), 6u);
  EXPECT_EQ((std::vector<float>(values.begin(), values.begin() + 6)),
            (std::vector<float>{1, 2, 3, 1, 2, 3}));
  EXPECT_GE(gate_messages, 2);
}

TEST_F(TrajectoryPublisherTest, RejectsInvalidConfigsBeforePlayback) {
  EXPECT_THROW(ros2_utils::CreateValidatedNode("empty", 7, config::Config()),
               std::invalid_argument);
  EXPECT_THROW(ros2_utils::CreateValidatedNode("wrong_id", 8, Config()), std::invalid_argument);
  auto config = Config();
  auto* entry = config.mutable_robot()->mutable_trajectories()->mutable_single_trajectories(0);
  auto* publisher = entry->mutable_node()->add_publishers();
  publisher->set_topic("/trajectory_test/a");
  publisher->set_ros2_data_type(ros2::data_type::FLOAT64);
  EXPECT_THROW(ros2_utils::CreateValidatedNode("wrong_type", 7, config), std::invalid_argument);
  entry->mutable_node()->clear_publishers();
  auto* waypoint = entry->mutable_trajectory()->mutable_waypoints(0);
  waypoint->mutable_action()->set_position(std::numeric_limits<float>::quiet_NaN());
  EXPECT_THROW(ros2_utils::CreateValidatedNode("nan", 7, config), std::invalid_argument);
  waypoint->mutable_action()->set_preset(robot::action::PRESET_IDLE_POSITION);
  EXPECT_THROW(ros2_utils::CreateValidatedNode("preset", 7, config), std::invalid_argument);
  waypoint->mutable_action()->set_position(1);
  waypoint->set_timestamp_sec(-1);
  EXPECT_THROW(ros2_utils::CreateValidatedNode("negative_time", 7, config), std::invalid_argument);
  for (auto& point : *entry->mutable_trajectory()->mutable_waypoints()) point.set_timestamp_sec(0);
  EXPECT_THROW(ros2_utils::CreateValidatedNode("zero_duration", 7, config), std::invalid_argument);
}
}  // namespace
