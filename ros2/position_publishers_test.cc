#include "ros2/position_publishers.h"

#include <chrono>
#include <cstdlib>
#include <thread>

#include "gtest/gtest.h"
#include "robot/board/factory/board_factory.h"

namespace {
TEST(PositionPublishersTest, PublishesFeedbackFromTheBoardsAlreadyOpenInThisProcess) {
  const char* test_tmpdir = std::getenv("TEST_TMPDIR");
  ASSERT_NE(test_tmpdir, nullptr);
  ASSERT_EQ(setenv("ROS_LOG_DIR", test_tmpdir, 1), 0);
  rclcpp::init(0, nullptr);
  {
    config::Robot robot;
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
    sensor->mutable_node()->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
    auto* topic = sensor->mutable_node()->add_publishers();
    topic->set_topic("position_feedback_test");
    topic->set_ros2_data_type(ros2::data_type::FLOAT32);
    topic->set_publish_rate_hz(30);

    auto existing_board = robot::board::BoardFactory::GetOrCreate(*board);
    ASSERT_TRUE(existing_board.ok()) << existing_board.status();
    auto channel = (*existing_board)->OpenChannel(1);
    ASSERT_TRUE(channel.ok()) << channel.status();
    ASSERT_TRUE((*channel)->Enable().ok());
    ASSERT_TRUE((*channel)->SetTarget(robot::board::TargetMode::kPosition, 2048.0f).ok());

    auto node = std::make_shared<rclcpp::Node>("position_feedback_test");
    bool received = false;
    auto subscription = node->create_subscription<std_msgs::msg::Float32>(
        topic->topic(),
        ros2_utils::CreateQosSetting(sensor->node().qos_setting()),
        [&received](std_msgs::msg::Float32::ConstSharedPtr message) {
          EXPECT_FLOAT_EQ(message->data, 2048.0f);
          received = true;
        });
    ros2_utils::PositionPublishers publishers(*node, 1, robot);
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!received && std::chrono::steady_clock::now() < deadline) {
      executor.spin_some();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(received);
  }
  robot::board::BoardFactory::ResetForTesting();
  rclcpp::shutdown();
}
}  // namespace
