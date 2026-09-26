#include "ros2/utils/packet_parser.h"

#include <set>
#include <string>

#include "google/protobuf/descriptor.h"
#include "gtest/gtest.h"

namespace {
TEST(PacketParserTest, EveryScalarActionHasATopicConversion) {
  const auto* descriptor = robot::action::ActionPacket::descriptor();
  const auto* oneof = descriptor->FindOneofByName("action_type");
  ASSERT_NE(oneof, nullptr);
  std::set<std::string> scalar_fields;
  for (int i = 0; i < oneof->field_count(); ++i) {
    const auto* field = oneof->field(i);
    if (field->type() != google::protobuf::FieldDescriptor::TYPE_FLOAT) continue;
    scalar_fields.insert(field->name());
    auto packet = ros2_utils::ActionPacketFromFloat(12.0f, "/joint/" + field->name(), true);
    ASSERT_TRUE(packet.ok()) << packet.status();
    EXPECT_EQ(packet->GetReflection()->GetOneofFieldDescriptor(*packet, oneof), field);
    EXPECT_FLOAT_EQ(packet->GetReflection()->GetFloat(*packet, field), 12.0f);
    EXPECT_EQ(packet->normalized(), field->name() == "position");
  }
  EXPECT_EQ(scalar_fields, (std::set<std::string>{"position", "speed", "torque", "dc"}));
  EXPECT_FALSE(ros2_utils::ActionPacketFromFloat(1, "/joint/unknown").ok());
  EXPECT_FALSE(ros2_utils::DeviceIdFromTopic("position").ok());
  auto device = ros2_utils::DeviceIdFromTopic("/joint/position");
  ASSERT_TRUE(device.ok());
  EXPECT_EQ(*device, "joint");
}

TEST(PacketParserTest, NormalizesSimpleAndComplexPositionsOnlyWhenRequested) {
  robot::action::ActionPacket packet;
  packet.set_position(0);
  ros2_utils::DenormalizeActionPacket(packet, 100, 200);
  EXPECT_FLOAT_EQ(packet.position(), 0);
  packet.set_normalized(true);
  ros2_utils::DenormalizeActionPacket(packet, 100, 200);
  EXPECT_FLOAT_EQ(packet.position(), 150);
  packet.mutable_complex()->set_position(-2);
  packet.mutable_complex()->set_speed(7);
  ros2_utils::DenormalizeActionPacket(packet, 100, 200);
  EXPECT_FLOAT_EQ(packet.complex().position(), 100);
  EXPECT_FLOAT_EQ(packet.complex().speed(), 7);
  EXPECT_FLOAT_EQ(ros2_utils::DenormalizePositionValue(2, 100, 200), 200);
}

TEST(PacketParserTest, ValidatesPerceptionPayloads) {
  robot::perception::PerceptionPacket packet;
  EXPECT_FALSE(ros2_utils::RequirePerceptionPosition(packet).ok());
  EXPECT_FALSE(ros2_utils::RequirePerceptionImage(packet).ok());
  EXPECT_FALSE(ros2_utils::RequirePerceptionPointCloud(packet).ok());
  packet.mutable_position()->set_position(5);
  auto position = ros2_utils::RequirePerceptionPosition(packet);
  ASSERT_TRUE(position.ok());
  EXPECT_FLOAT_EQ(*position, 5);
  packet.mutable_image();
  EXPECT_TRUE(ros2_utils::RequirePerceptionImage(packet).ok());
  packet.mutable_point_cloud();
  EXPECT_TRUE(ros2_utils::RequirePerceptionPointCloud(packet).ok());
}
}  // namespace
