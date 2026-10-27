#include "ros2/camera_image.h"

#include "gtest/gtest.h"
namespace {
TEST(CameraImageTest, ConvertsBgrAndRgbWithoutReadingPastPacket) {
  robot::perception::PerceptionPacket p;
  auto* im = p.mutable_image();
  im->set_width(1);
  im->set_height(1);
  im->set_channels(3);
  im->set_encoding("bgr8");
  im->set_data("\x01\x02\x03", 3);
  auto r = ros2_utils::CameraPacketImage(p);
  ASSERT_TRUE(r.ok()) << r.status();
  EXPECT_EQ(r->data, (std::vector<uint8_t>{3, 2, 1}));
  im->set_encoding("rgb8");
  r = ros2_utils::CameraPacketImage(p);
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r->data, (std::vector<uint8_t>{1, 2, 3}));
  for (const auto& bad : {"mono8", "jpeg"}) {
    im->set_encoding(bad);
    EXPECT_FALSE(ros2_utils::CameraPacketImage(p).ok());
  }
  im->set_encoding("bgr8");
  im->set_data("x");
  EXPECT_FALSE(ros2_utils::CameraPacketImage(p).ok());
  im->set_data("1234");
  EXPECT_FALSE(ros2_utils::CameraPacketImage(p).ok());
  im->set_channels(4);
  EXPECT_FALSE(ros2_utils::CameraPacketImage(p).ok());
  im->set_channels(3);
  im->set_width(0xffffffff);
  im->set_height(0xffffffff);
  EXPECT_FALSE(ros2_utils::CameraPacketImage(p).ok());
}
TEST(CameraImageTest, RejectsMissingImage) {
  robot::perception::PerceptionPacket p;
  EXPECT_FALSE(ros2_utils::CameraPacketImage(p).ok());
}
TEST(CameraImageTest, RejectsNegativePairEvenWhenWrappedProductMatchesData) {
  robot::perception::PerceptionPacket p;
  auto* im = p.mutable_image();
  im->set_width(-1);
  im->set_height(-1);
  im->set_channels(3);
  im->set_encoding("rgb8");
  im->set_data("abc");
  EXPECT_FALSE(ros2_utils::CameraPacketImage(p).ok());
}

}  // namespace
