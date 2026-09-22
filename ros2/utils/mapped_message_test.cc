#include "ros2/utils/mapped_message.h"

#include <limits>

#include "geometry_msgs/msg/pose_stamped.hpp"
#include "gtest/gtest.h"
#include "rclcpp/serialization.hpp"
#include "rclcpp/typesupport_helpers.hpp"
#include "ros2/utils/numeric_message_config.h"
#include "rosidl_typesupport_introspection_cpp/field_types.hpp"
#include "rosidl_typesupport_introspection_cpp/message_introspection.hpp"
#include "sensor_msgs/msg/joint_state.hpp"
#include "std_msgs/msg/float64.hpp"
#include "std_msgs/msg/u_int64.hpp"

namespace {
using ros2_utils::MappedMessage;
ros2::node::ScalarMapping Mapping(const std::string& path) {
  ros2::node::ScalarMapping mapping;
  mapping.set_field_path(path);
  return mapping;
}
template <typename T>
rclcpp::SerializedMessage Serialize(const T& message) {
  rclcpp::SerializedMessage serialized;
  rclcpp::Serialization<T>().serialize_message(&message, &serialized);
  return serialized;
}
template <typename T>
T Deserialize(const rclcpp::SerializedMessage& serialized) {
  T message;
  rclcpp::Serialization<T>().deserialize_message(&serialized, &message);
  return message;
}
TEST(MappedMessageTest, PreservesLegacyFloat32Only) {
  EXPECT_TRUE(MappedMessage::Create(ros2::data_type::FLOAT32, {}, true).ok());
  EXPECT_FALSE(MappedMessage::Create(ros2::data_type::FLOAT64, {}, true).ok());
}
TEST(MappedMessageTest, EncodesNestedFieldAndMetadataWithUnits) {
  auto mapping = Mapping("pose.position.x");
  mapping.set_scale(0.001);
  auto* frame = mapping.add_constants();
  frame->set_field_path("header.frame_id");
  frame->set_text("map");
  auto* orientation = mapping.add_constants();
  orientation->set_field_path("pose.orientation.w");
  orientation->set_number(1);
  auto codec = MappedMessage::Create(ros2::data_type::POSE_STAMPED, mapping, true);
  ASSERT_TRUE(codec.ok()) << codec.status();
  auto encoded = (*codec)->Encode(1250);
  ASSERT_TRUE(encoded.ok()) << encoded.status();
  const auto message = Deserialize<geometry_msgs::msg::PoseStamped>(*encoded);
  EXPECT_DOUBLE_EQ(message.pose.position.x, 1.25);
  EXPECT_EQ(message.header.frame_id, "map");
  EXPECT_DOUBLE_EQ(message.pose.orientation.w, 1);
}
TEST(MappedMessageTest, SelectsJointArrayElementAndRejectsMissingElement) {
  auto mapping = Mapping("position[1]");
  mapping.set_scale(1000);
  auto codec = MappedMessage::Create(ros2::data_type::JOINT_STATE, mapping, false);
  ASSERT_TRUE(codec.ok()) << codec.status();
  sensor_msgs::msg::JointState joint;
  joint.position = {9, 0.125};
  auto result = (*codec)->Decode(Serialize(joint));
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_FLOAT_EQ(*result, 125);
  joint.position.resize(1);
  EXPECT_FALSE((*codec)->Decode(Serialize(joint)).ok());
}
TEST(MappedMessageTest, CreatesArraysAndStringMetadata) {
  auto mapping = Mapping("position[0]");
  auto* name = mapping.add_constants();
  name->set_field_path("name[0]");
  name->set_text("elbow");
  auto codec = MappedMessage::Create(ros2::data_type::JOINT_STATE, mapping, true);
  ASSERT_TRUE(codec.ok()) << codec.status();
  auto result = (*codec)->Encode(1.5f);
  ASSERT_TRUE(result.ok()) << result.status();
  auto message = Deserialize<sensor_msgs::msg::JointState>(*result);
  ASSERT_EQ(message.name.size(), 1);
  EXPECT_EQ(message.name[0], "elbow");
  ASSERT_EQ(message.position.size(), 1);
  EXPECT_DOUBLE_EQ(message.position[0], 1.5);
  EXPECT_TRUE(message.effort.empty());
}
TEST(MappedMessageTest, RejectsInvalidFieldsAndConstantsBeforeHardware) {
  for (const auto& path : {"missing.x", "pose.position", "pose.position.x[0]", "pose..x"})
    EXPECT_FALSE(MappedMessage::Create(ros2::data_type::POSE, Mapping(path), true).ok());
  EXPECT_FALSE(MappedMessage::Create(ros2::data_type::STRING, Mapping("data"), true).ok());
  EXPECT_FALSE(MappedMessage::Create(ros2::data_type::EMPTY, Mapping("data"), true).ok());
  EXPECT_FALSE(
      MappedMessage::Create(ros2::data_type::JOINT_STATE, Mapping("position[4096]"), true).ok());
  EXPECT_FALSE(
      MappedMessage::Create(ros2::data_type::IMU, Mapping("orientation_covariance[9]"), true).ok());
  auto mapping = Mapping("data");
  auto* constant = mapping.add_constants();
  constant->set_field_path("data");
  constant->set_number(1);
  EXPECT_FALSE(MappedMessage::Create(ros2::data_type::FLOAT32, mapping, true).ok());
  EXPECT_FALSE(MappedMessage::Create(ros2::data_type::FLOAT32, mapping, false).ok());
}
TEST(MappedMessageTest, RejectsNonFiniteOverflowAndLossyIntegerConversions) {
  auto integers = MappedMessage::Create(ros2::data_type::UINT8, Mapping("data"), true);
  ASSERT_TRUE(integers.ok());
  for (float value : {-1.f,
                      256.f,
                      1.25f,
                      std::numeric_limits<float>::infinity(),
                      std::numeric_limits<float>::quiet_NaN()})
    EXPECT_FALSE((*integers)->Encode(value).ok());
  auto boolean = MappedMessage::Create(ros2::data_type::BOOL, Mapping("data"), true);
  ASSERT_TRUE(boolean.ok());
  EXPECT_FALSE((*boolean)->Encode(2).ok());
  EXPECT_TRUE((*boolean)->Encode(1).ok());
  auto floating = MappedMessage::Create(ros2::data_type::FLOAT64, Mapping("data"), false);
  ASSERT_TRUE(floating.ok());
  std_msgs::msg::Float64 message;
  message.data = std::numeric_limits<double>::max();
  EXPECT_FALSE((*floating)->Decode(Serialize(message)).ok());
  message.data = std::numeric_limits<double>::quiet_NaN();
  EXPECT_FALSE((*floating)->Decode(Serialize(message)).ok());
  auto wide = MappedMessage::Create(ros2::data_type::UINT64, Mapping("data"), false);
  ASSERT_TRUE(wide.ok());
  std_msgs::msg::UInt64 integer;
  integer.data = 16777217;
  EXPECT_FALSE((*wide)->Decode(Serialize(integer)).ok());
  integer.data = 16777216;
  EXPECT_TRUE((*wide)->Decode(Serialize(integer)).ok());
}

// Exercise every enum's installed type support, including nested message arrays.
// Empty and String genuinely have no numeric field and must reject mappings.
std::string FirstNumericField(const rosidl_typesupport_introspection_cpp::MessageMembers* members) {
  namespace intro = rosidl_typesupport_introspection_cpp;
  for (size_t i = 0; i < members->member_count_; ++i) {
    const auto& field = members->members_[i];
    std::string path = field.name_;
    if (field.is_array_) path += "[0]";
    if (field.type_id_ == intro::ROS_TYPE_MESSAGE) {
      auto nested =
          FirstNumericField(static_cast<const intro::MessageMembers*>(field.members_->data));
      if (!nested.empty()) return path + "." + nested;
    } else if (field.type_id_ != intro::ROS_TYPE_STRING &&
               field.type_id_ != intro::ROS_TYPE_WSTRING) {
      return path;
    }
  }
  return "";
}
TEST(MappedMessageTest, ResolvesEveryConfiguredRosMessageType) {
  for (int i = 1; i <= ros2::data_type::Ros2DataType_MAX; ++i) {
    auto type = static_cast<ros2::data_type::Ros2DataType>(i);
    const auto name = ros2_utils::RosMessageType(type);
    SCOPED_TRACE(name);
    ASSERT_FALSE(name.empty());
    auto library = rclcpp::get_typesupport_library(name, "rosidl_typesupport_introspection_cpp");
    auto* handle =
        rclcpp::get_typesupport_handle(name, "rosidl_typesupport_introspection_cpp", *library);
    const auto field = FirstNumericField(
        static_cast<const rosidl_typesupport_introspection_cpp::MessageMembers*>(handle->data));
    if (field.empty()) {
      EXPECT_FALSE(MappedMessage::Create(type, Mapping("data"), true).ok());
      continue;
    }
    auto codec = MappedMessage::Create(type, Mapping(field), true);
    ASSERT_TRUE(codec.ok()) << codec.status();
    auto encoded = (*codec)->Encode(1);
    ASSERT_TRUE(encoded.ok()) << encoded.status();
    auto decoded = (*codec)->Decode(*encoded);
    ASSERT_TRUE(decoded.ok()) << decoded.status();
    EXPECT_FLOAT_EQ(*decoded, 1);
  }
}
}  // namespace
