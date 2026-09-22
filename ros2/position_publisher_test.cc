#include <chrono>
#include <cstdlib>
#include <thread>

#include "config/proto/config.pb.h"
#include "gtest/gtest.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/board/factory/board_factory.h"
#include "ros2/utils/qos_setting.h"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_msgs/msg/float32.hpp"
#include "std_msgs/msg/float64.hpp"

std::shared_ptr<rclcpp::Node> MakePositionPublisherForTest(const config::Config& config);

namespace {
TEST(PositionPublisherTest, PublishesFeedbackFromTheBoardsAlreadyOpenInThisProcess) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  ASSERT_NE(test_tmpdir, nullptr);
  ASSERT_EQ(setenv("ROS_LOG_DIR", test_tmpdir, 1), 0);
  rclcpp::init(0, nullptr);
  {
    config::Config config;
    auto& robot = *config.mutable_robot();
    auto* board = robot.add_boards();
    board->set_name("shared_board");
    board->set_board_type(robot::board::MOCK);
    board->add_channels()->set_index(1);
    auto* sensor = robot.mutable_perceptions()->add_single_perceptions();
    sensor->set_sensor_name("joint");
    sensor->set_sensor_type(robot::perception::POSITION);
    sensor->mutable_sts3215_encoder_config()->set_board_name("shared_board");
    sensor->mutable_sts3215_encoder_config()->set_channel(1);
    sensor->mutable_node()->set_id(1);
    sensor->mutable_node()->set_node_type(ros2::node::POSITION_PUBLISHER);
    auto* topic = sensor->mutable_node()->add_publishers();
    topic->set_topic("position_feedback_test");
    topic->set_ros2_data_type(ros2::data_type::FLOAT32);
    topic->set_publish_rate_hz(30);

    auto* mapped = sensor->mutable_node()->add_publishers();
    mapped->set_topic("position_feedback_double");
    mapped->set_ros2_data_type(ros2::data_type::FLOAT64);
    mapped->set_publish_rate_hz(30);
    auto* joint = sensor->mutable_node()->add_publishers();
    joint->set_topic("position_feedback_joint");
    joint->set_ros2_data_type(ros2::data_type::JOINT_STATE);
    joint->set_publish_rate_hz(30);
    auto existing_board = robot::board::BoardFactory::GetOrCreate(*board);
    ASSERT_TRUE(existing_board.ok()) << existing_board.status();
    auto channel = (*existing_board)->OpenChannel(1);
    ASSERT_TRUE(channel.ok()) << channel.status();
    ASSERT_TRUE((*channel)->Enable().ok());
    ASSERT_TRUE((*channel)->SetTarget(robot::board::TargetMode::kPosition, 2048.0f).ok());

    auto node = MakePositionPublisherForTest(config);
    bool joint_received = false;
    auto joint_subscription = node->create_subscription<sensor_msgs::msg::JointState>(
        joint->topic(),
        10,
        [&joint_received](sensor_msgs::msg::JointState::ConstSharedPtr message) {
          ASSERT_EQ(message->name.size(), 1);
          EXPECT_EQ(message->name[0], "joint");
          ASSERT_EQ(message->position.size(), 1);
          EXPECT_DOUBLE_EQ(message->position[0], 3.14159265358979323846);
          EXPECT_TRUE(message->velocity.empty());
          EXPECT_TRUE(message->effort.empty());
          EXPECT_GT(message->header.stamp.sec, 0);
          joint_received = true;
        });
    bool mapped_received = false;
    auto double_subscription = node->create_subscription<std_msgs::msg::Float64>(
        mapped->topic(), 10, [&mapped_received](std_msgs::msg::Float64::ConstSharedPtr message) {
          EXPECT_DOUBLE_EQ(message->data, 2048.0);
          mapped_received = true;
        });
    bool received = false;
    auto subscription = node->create_subscription<std_msgs::msg::Float32>(
        topic->topic(),
        ros2_utils::CreateQosSetting(sensor->node().qos_setting()),
        [&received](std_msgs::msg::Float32::ConstSharedPtr message) {
          EXPECT_FLOAT_EQ(message->data, 2048.0f);
          received = true;
        });
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while ((!received || !mapped_received || !joint_received) &&
           std::chrono::steady_clock::now() < deadline) {
      executor.spin_some();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(received);
    EXPECT_TRUE(mapped_received);
    EXPECT_TRUE(joint_received);
  }
  robot::board::BoardFactory::ResetForTesting();
  rclcpp::shutdown();
}
}  // namespace
