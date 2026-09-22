#include <chrono>
#include <cstdlib>
#include <limits>
#include <thread>

#include "config/proto/config.pb.h"
#include "gtest/gtest.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/board/factory/board_factory.h"
#include "std_msgs/msg/float64.hpp"

std::shared_ptr<rclcpp::Node> MakeActuatorSubscriberForTest(const config::Config& config);

namespace {
TEST(ActuatorSubscriberTest, MapsExternalTopicAndRejectsInvalidValuesBeforeDriver) {
  ASSERT_EQ(setenv("ROS_LOG_DIR", std::getenv("TEST_TMPDIR"), 1), 0);
  rclcpp::init(0, nullptr);
  {
    config::Config config;
    auto* board = config.mutable_robot()->add_boards();
    board->set_name("mock");
    board->set_board_type(robot::board::MOCK);
    auto* channel_config = board->add_channels();
    channel_config->set_index(1);
    channel_config->set_drive(robot::board::STEP_DIR);
    auto* action = config.mutable_robot()->mutable_actions()->add_single_actions();
    action->set_action_type(robot::action::ACTUATOR);
    auto* actuator = action->mutable_actuator();
    actuator->set_actuator_name("joint");
    actuator->set_board_name("mock");
    actuator->set_channel(1);
    actuator->set_motor_type(robot::action::MOTOR_STEPPER_NEMA17);
    actuator->mutable_stepper_config()->set_steps_per_degree(1);
    actuator->mutable_stepper_config()->set_gear_ratio(1);
    actuator->set_operational_lower_limit(0);
    actuator->set_operational_upper_limit(100);
    auto* node = action->mutable_node();
    node->set_id(1);
    node->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
    auto* subscription = node->add_subscriptions();
    subscription->set_topic("external_controller_target");
    subscription->set_ros2_data_type(ros2::data_type::FLOAT64);
    subscription->set_command("position");
    subscription->mutable_scalar_mapping()->set_field_path("data");
    subscription->mutable_scalar_mapping()->set_scale(2);
    auto hardware = robot::board::BoardFactory::GetOrCreate(*board);
    ASSERT_TRUE(hardware.ok());
    auto channel = (*hardware)->OpenChannel(1);
    ASSERT_TRUE(channel.ok());
    auto subscriber = MakeActuatorSubscriberForTest(config);
    auto publisher =
        subscriber->create_publisher<std_msgs::msg::Float64>(subscription->topic(), 10);
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(subscriber);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (publisher->get_subscription_count() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
      executor.spin_some();
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_GT(publisher->get_subscription_count(), 0);
    auto send = [&](double value) {
      std_msgs::msg::Float64 message;
      message.data = value;
      publisher->publish(message);
      const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(100);
      while (std::chrono::steady_clock::now() < until) {
        executor.spin_some();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
    };
    send(25);  // ROS 25 -> driver/channel 50.
    auto feedback = (*channel)->ReadFeedback();
    ASSERT_TRUE(feedback.ok());
    EXPECT_FLOAT_EQ(feedback->position, 50.0f);
    for (double bad :
         {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::max(), 1000.0}) {
      send(bad);
      feedback = (*channel)->ReadFeedback();
      ASSERT_TRUE(feedback.ok());
      EXPECT_FLOAT_EQ(feedback->position, 50.0f);
    }
  }
  robot::board::BoardFactory::ResetForTesting();
  rclcpp::shutdown();
}

TEST(ActuatorSubscriberTest, ResolvesAllMappingsBeforeEnablingAnyChannel) {
  ASSERT_EQ(setenv("ROS_LOG_DIR", std::getenv("TEST_TMPDIR"), 1), 0);
  rclcpp::init(0, nullptr);
  {
    config::Config config;
    auto* board = config.mutable_robot()->add_boards();
    board->set_name("preflight_mock");
    board->set_board_type(robot::board::MOCK);
    board->add_channels()->set_index(1);
    board->mutable_channels(0)->set_drive(robot::board::STEP_DIR);
    auto* action = config.mutable_robot()->mutable_actions()->add_single_actions();
    action->set_action_type(robot::action::ACTUATOR);
    auto* actuator = action->mutable_actuator();
    actuator->set_actuator_name("joint");
    actuator->set_board_name("preflight_mock");
    actuator->set_channel(1);
    actuator->set_motor_type(robot::action::MOTOR_STEPPER_NEMA17);
    actuator->mutable_stepper_config()->set_steps_per_degree(1);
    actuator->mutable_stepper_config()->set_gear_ratio(1);
    actuator->set_operational_upper_limit(100);
    auto* node = action->mutable_node();
    node->set_id(1);
    node->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
    auto* valid = node->add_subscriptions();
    valid->set_topic("joint/position");
    valid->set_ros2_data_type(ros2::data_type::FLOAT32);
    auto* invalid = node->add_subscriptions();
    invalid->set_topic("other");
    invalid->set_command("position");
    invalid->set_ros2_data_type(ros2::data_type::FLOAT64);
    invalid->mutable_scalar_mapping()->set_field_path("nonexistent");
    auto hardware = robot::board::BoardFactory::GetOrCreate(*board);
    ASSERT_TRUE(hardware.ok());
    auto channel = (*hardware)->OpenChannel(1);
    ASSERT_TRUE(channel.ok());
    EXPECT_THROW(MakeActuatorSubscriberForTest(config), std::invalid_argument);
    EXPECT_EQ((*channel)->SetTarget(robot::board::TargetMode::kPosition, 1).code(),
              absl::StatusCode::kFailedPrecondition);
  }
  robot::board::BoardFactory::ResetForTesting();
  rclcpp::shutdown();
}
}  // namespace
