#include <chrono>
#include <cstdlib>
#include <limits>
#include <optional>
#include <thread>

#include "gtest/gtest.h"
#include "rclcpp/rclcpp.hpp"
#include "ros2/utils/packet_parser.h"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_msgs/msg/bool.hpp"
#include "std_msgs/msg/float64.hpp"
#include "std_msgs/msg/float64_multi_array.hpp"
#include "std_msgs/msg/int8.hpp"
#include "std_msgs/msg/u_int64.hpp"

namespace {
class PacketParserCppTest : public testing::Test {
 protected:
  void SetUp() override {
    ASSERT_EQ(setenv("ROS_LOG_DIR", std::getenv("TEST_TMPDIR"), 1), 0);
    rclcpp::init(0, nullptr);
    node = std::make_shared<rclcpp::Node>("packet_parser_test");
    executor = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    executor->add_node(node);
    auto* actuator = action.mutable_actuator();
    actuator->set_actuator_name("elbow");
    actuator->set_motor_type(robot::action::MOTOR_STEPPER_NEMA17);
    config.set_topic("elbow/position");
  }
  void TearDown() override {
    executor->remove_node(node);
    executor.reset();
    node.reset();
    rclcpp::shutdown();
  }
  template <typename Predicate>
  void SpinUntil(Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!ready() && std::chrono::steady_clock::now() < deadline) {
      executor->spin_some();
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    EXPECT_TRUE(ready());
  }
  template <typename Message>
  absl::StatusOr<robot::action::ActionPacket> Send(ros2::data_type::Ros2DataType type,
                                                   const Message& message) {
    config.set_ros2_data_type(type);
    std::optional<absl::StatusOr<robot::action::ActionPacket>> received;
    auto subscription = ros2_utils::CreateActionMessageSubscription(
        *node, config, action, [&](auto result) { received = std::move(result); });
    if (!subscription.ok()) return subscription.status();
    auto publisher = node->create_publisher<Message>(config.topic(), 10);
    SpinUntil([&] { return publisher->get_subscription_count() != 0; });
    publisher->publish(message);
    SpinUntil([&] { return received.has_value(); });
    if (!received) return absl::DeadlineExceededError("No command received");
    return *received;
  }
  std::shared_ptr<rclcpp::Node> node;
  std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor;
  robot::action::SingleAction action;
  ros2::node::Subscription config;
};
TEST_F(PacketParserCppTest, Float64UsesNativeUnitsAndRejectsNonFiniteOrOverflow) {
  std_msgs::msg::Float64 message;
  message.data = 12.5;
  auto result = Send(ros2::data_type::FLOAT64, message);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_FLOAT_EQ(result->position(), 12.5f);
  for (double invalid : {std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(),
                         std::numeric_limits<double>::max(),
                         std::numeric_limits<double>::denorm_min()}) {
    message.data = invalid;
    EXPECT_FALSE(Send(ros2::data_type::FLOAT64, message).ok());
  }
}
TEST_F(PacketParserCppTest, RejectsIntegerCommandPrecisionLoss) {
  std_msgs::msg::UInt64 message;
  message.data = 16777217;
  EXPECT_FALSE(Send(ros2::data_type::UINT64, message).ok());
  message.data = 16777216;
  auto result = Send(ros2::data_type::UINT64, message);
  ASSERT_TRUE(result.ok());
  EXPECT_FLOAT_EQ(result->position(), 16777216.f);
}
TEST_F(PacketParserCppTest, ArraysContainExactlyOneValue) {
  std_msgs::msg::Float64MultiArray message;
  message.data = {8.5};
  auto result = Send(ros2::data_type::FLOAT64_MULTI_ARRAY, message);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_FLOAT_EQ(result->position(), 8.5);
  message.data = {};
  EXPECT_FALSE(Send(ros2::data_type::FLOAT64_MULTI_ARRAY, message).ok());
  message.data = {1, 2};
  EXPECT_FALSE(Send(ros2::data_type::FLOAT64_MULTI_ARRAY, message).ok());
  message.data = {1};
  message.layout.data_offset = 1;
  EXPECT_FALSE(Send(ros2::data_type::FLOAT64_MULTI_ARRAY, message).ok());
}
TEST_F(PacketParserCppTest, JointStateSelectsByNameAndConvertsRadiansToDriverUnits) {
  config.set_topic("joint_commands");
  sensor_msgs::msg::JointState message;
  message.name = {"other", "elbow"};
  message.position = {2, 1.5707963267948966};
  auto result = Send(ros2::data_type::JOINT_STATE, message);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_FLOAT_EQ(result->position(), 90);
  action.mutable_actuator()->set_motor_type(robot::action::MOTOR_STS3215);
  result = Send(ros2::data_type::JOINT_STATE, message);
  ASSERT_TRUE(result.ok());
  EXPECT_FLOAT_EQ(result->position(), 1024);
  message.name = {"elbow", "elbow"};
  EXPECT_FALSE(Send(ros2::data_type::JOINT_STATE, message).ok());
  message.name = {"other", "missing"};
  EXPECT_FALSE(Send(ros2::data_type::JOINT_STATE, message).ok());
  message.name = {"elbow"};
  EXPECT_FALSE(Send(ros2::data_type::JOINT_STATE, message).ok());
  message.position = {1};
  message.effort = {1};
  EXPECT_FALSE(Send(ros2::data_type::JOINT_STATE, message).ok());
}
TEST_F(PacketParserCppTest, BoolIsAnEnableGateNotAPositionOrContinuousTorque) {
  std_msgs::msg::Bool message;
  message.data = true;
  EXPECT_FALSE(Send(ros2::data_type::BOOL, message).ok());
  config.set_topic("elbow/torque");
  auto result = Send(ros2::data_type::BOOL, message);
  ASSERT_TRUE(result.ok());
  EXPECT_FLOAT_EQ(result->torque(), 1);
  message.data = false;
  result = Send(ros2::data_type::BOOL, message);
  ASSERT_TRUE(result.ok());
  EXPECT_FLOAT_EQ(result->torque(), 0);
  action.mutable_actuator()->set_motor_type(robot::action::MOTOR_TI_DEMO);
  EXPECT_FALSE(Send(ros2::data_type::BOOL, message).ok());
}
TEST_F(PacketParserCppTest, IntegerFeedbackDoesNotTruncateOrWrap) {
  robot::perception::SinglePerception sensor;
  ros2::node::Publisher config;
  config.set_topic("integer_feedback");
  config.set_ros2_data_type(ros2::data_type::INT8);
  auto publisher = ros2_utils::CreatePositionMessagePublisher(*node, config, sensor);
  ASSERT_TRUE(publisher.ok());
  std::optional<int> value;
  auto receiver = node->create_subscription<std_msgs::msg::Int8>(
      config.topic(), 10, [&](std_msgs::msg::Int8::ConstSharedPtr message) {
        value = message->data;
      });
  EXPECT_FALSE((*publisher)(128).ok());
  EXPECT_FALSE((*publisher)(-129).ok());
  EXPECT_FALSE((*publisher)(1.5).ok());
  EXPECT_FALSE((*publisher)(std::numeric_limits<float>::quiet_NaN()).ok());
  // Repeat the valid sample until ROS discovery completes.
  SpinUntil([&] {
    EXPECT_TRUE((*publisher)(42).ok());
    return value.has_value();
  });
  ASSERT_TRUE(value.has_value());
  EXPECT_EQ(*value, 42);
}
}  // namespace
