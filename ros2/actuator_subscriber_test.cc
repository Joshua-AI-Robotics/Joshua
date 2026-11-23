#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "config/validation.h"
#include "gtest/gtest.h"
#include "rclcpp/version.h"
#include "robot/board/factory/board_factory.h"
#include "std_msgs/msg/float32.hpp"

#define JOSHUA_NODE_TEST
#include "ros2/actuator_subscriber.cc"

namespace {
using namespace std::chrono_literals;

class ActuatorSubscriberTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_NE(std::getenv("TEST_TMPDIR"), nullptr);
    ASSERT_EQ(setenv("ROS_LOG_DIR", std::getenv("TEST_TMPDIR"), 1), 0);
    ASSERT_EQ(setenv("ROS_DOMAIN_ID", RCLCPP_VERSION_MAJOR >= 20 ? "118" : "117", 1), 0);
    ASSERT_EQ(setenv("ROS_LOCALHOST_ONLY", "1", 1), 0);
    rclcpp::init(0, nullptr);
    executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    prefix_ = "multi_actuator_" + std::to_string(getpid());
    auto* board = config_.mutable_robot()->add_boards();
    board->set_name("multi_actuator_mock");
    board->set_board_type(robot::board::MOCK);
    for (int index = 0; index < 3; ++index) {
      auto* channel = board->add_channels();
      channel->set_index(index);
      channel->set_drive(robot::board::STEP_DIR);
      auto* action = config_.mutable_robot()->mutable_actions()->add_single_actions();
      action->set_action_type(robot::action::ACTUATOR);
      action->mutable_node()->set_id(index == 2 ? 2 : 1);
      action->mutable_node()->set_node_type(ros2::node::ACTUATOR_SUBSCRIBER);
      auto* actuator = action->mutable_actuator();
      actuator->set_actuator_name(Name(index));
      actuator->set_id(index);
      actuator->set_motor_type(robot::action::MOTOR_STEPPER_NEMA17);
      actuator->set_board_name(board->name());
      actuator->set_channel(index);
      actuator->set_operational_lower_limit(-90);
      actuator->set_operational_upper_limit(90);
      actuator->mutable_stepper_config()->set_steps_per_degree(index + 2);
      actuator->mutable_stepper_config()->set_gear_ratio(1);
      actuator->mutable_stepper_config()->set_idle_position(index + 1);
      auto* position = action->mutable_node()->add_subscriptions();
      position->set_topic(Name(index) + "/position");
      position->set_ros2_data_type(ros2::data_type::FLOAT32);
      if (index == 0) {
        auto* speed = action->mutable_node()->add_subscriptions();
        speed->set_topic(Name(index) + "/speed");
        speed->set_ros2_data_type(ros2::data_type::FLOAT32);
      }
    }
    ASSERT_TRUE(config::ValidateConfig(config_).ok());
    auto cached = robot::board::BoardFactory::GetOrCreate(*board);
    ASSERT_TRUE(cached.ok()) << cached.status();
    // Keep channel handles after node destruction to observe its own teardown.
    for (int index = 0; index < 3; ++index) {
      auto channel = (*cached)->OpenChannel(index);
      ASSERT_TRUE(channel.ok()) << channel.status();
      channels_.push_back(*channel);
    }
    runtime_ = std::make_shared<ActionSubscriber>(prefix_ + "_runtime", 1, config_);
    client_ = std::make_shared<rclcpp::Node>(prefix_ + "_client");
    executor_->add_node(runtime_);
    executor_->add_node(client_);
  }

  void TearDown() override {
    if (executor_ && runtime_) executor_->remove_node(runtime_);
    if (executor_ && client_) executor_->remove_node(client_);
    runtime_.reset();
    client_.reset();
    executor_.reset();
    channels_.clear();
    robot::board::BoardFactory::ResetForTesting();
    rclcpp::shutdown();
  }

  std::string Name(int index) const {
    return prefix_ + "_joint_" + std::to_string(index);
  }

  bool WaitUntil(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) executor_->spin_once(5ms);
    return predicate();
  }

  config::Config config_;
  std::string prefix_;
  std::vector<std::shared_ptr<robot::board::BoardChannel>> channels_;
  std::shared_ptr<ActionSubscriber> runtime_;
  rclcpp::Node::SharedPtr client_;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

TEST_F(ActuatorSubscriberTest,
       HostsMultipleActuatorsAndSubscriptionsAndTearsDownEveryOwnedActuator) {
  using Message = std_msgs::msg::Float32;
  auto first = client_->create_publisher<Message>(Name(0) + "/position", 10);
  auto speed = client_->create_publisher<Message>(Name(0) + "/speed", 10);
  auto second = client_->create_publisher<Message>(Name(1) + "/position", 10);
  auto other_node = client_->create_publisher<Message>(Name(2) + "/position", 10);
  ASSERT_TRUE(WaitUntil([&] {
    return first->get_subscription_count() == 1 && speed->get_subscription_count() == 1 &&
           second->get_subscription_count() == 1;
  }));
  EXPECT_EQ(other_node->get_subscription_count(), 0u);

  Message message;
  message.data = 11;
  first->publish(message);
  message.data = 7;
  second->publish(message);
  message.data = 4.5;
  speed->publish(message);
  message.data = 19;
  other_node->publish(message);
  ASSERT_TRUE(WaitUntil([&] {
    const auto first_feedback = channels_[0]->ReadFeedback();
    const auto second_feedback = channels_[1]->ReadFeedback();
    return first_feedback.ok() && second_feedback.ok() && first_feedback->position == 22 &&
           first_feedback->velocity == 9 && second_feedback->position == 21;
  }));
  const auto second_feedback = channels_[1]->ReadFeedback();
  ASSERT_TRUE(second_feedback.ok());
  EXPECT_FLOAT_EQ(second_feedback->velocity, 0);
  const auto ignored_feedback = channels_[2]->ReadFeedback();
  ASSERT_TRUE(ignored_feedback.ok());
  EXPECT_FLOAT_EQ(ignored_feedback->position, 0);

  executor_->remove_node(runtime_);
  runtime_.reset();
  // Both owned actuators return to their individual idle positions, then disable.
  // Multiple subscriptions on the first actuator must not lose the second one.
  for (int index = 0; index < 2; ++index) {
    const auto feedback = channels_[index]->ReadFeedback();
    ASSERT_TRUE(feedback.ok());
    EXPECT_FLOAT_EQ(feedback->position, (index + 1) * (index + 2));
    EXPECT_EQ(channels_[index]->SetTarget(robot::board::TargetMode::kPosition, 1).code(),
              absl::StatusCode::kFailedPrecondition);
  }
  EXPECT_EQ(channels_[2]->SetTarget(robot::board::TargetMode::kPosition, 1).code(),
            absl::StatusCode::kFailedPrecondition);
}
}  // namespace
