#include "tools/mcp/ros_source_binding.h"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "gtest/gtest.h"

namespace {
class RosSourceBindingTest : public testing::Test {
 protected:
  void SetUp() override {
    char path[] = "/tmp/joshua-ros-binding-XXXXXX";
    directory_ = mkdtemp(path);
    ASSERT_FALSE(directory_.empty());
    root_ = open(directory_.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    ASSERT_GE(root_, 0);
    Write("camera/image", "webcam");
  }
  void TearDown() override {
    if (root_ >= 0) close(root_);
    std::filesystem::remove_all(directory_);
  }
  void Write(const std::string& topic, const std::string& name) {
    std::ofstream(directory_ + "/candidate.pbtxt")
        << "robot { perceptions { single_perceptions { sensor_name: \"" << name
        << "\" sensor_type: IMAGE node { id: 12 node_type: CAMERA_PUBLISHER "
        << "publishers { ros2_data_type: IMAGE topic: \"" << topic
        << "\" publish_rate_hz: 30 } } } } }";
  }
  std::string directory_;
  int root_ = -1;
};
TEST_F(RosSourceBindingTest, ResolvesConfiguredRosEndpointWithoutLauncherOrDriver) {
  const auto result =
      joshua::mcp::ResolveImageSource(root_, "candidate.pbtxt", "sensor:webcam", 21);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->topic, "/camera/image");
  EXPECT_EQ(result->node, "camera_publisher_node_12");
  EXPECT_EQ(result->domain, 21);
  EXPECT_EQ(result->ns, "/");
}
TEST_F(RosSourceBindingTest, ReadsEditedMappingAndRejectsMissingIdentity) {
  const auto first = joshua::mcp::ResolveImageSource(root_, "candidate.pbtxt", "sensor:webcam", 21);
  ASSERT_TRUE(first.ok());
  Write("other/image", "webcam");
  const auto second =
      joshua::mcp::ResolveImageSource(root_, "candidate.pbtxt", "sensor:webcam", 21);
  ASSERT_TRUE(second.ok());
  EXPECT_FALSE(*first == *second);
  EXPECT_FALSE(
      joshua::mcp::ResolveImageSource(root_, "candidate.pbtxt", "sensor:missing", 21).ok());
}
TEST_F(RosSourceBindingTest, RejectsUnconfinedAndAmbiguousSources) {
  EXPECT_FALSE(
      joshua::mcp::ResolveImageSource(root_, "../candidate.pbtxt", "sensor:webcam", 21).ok());
  ASSERT_EQ(symlink("candidate.pbtxt", (directory_ + "/alias.pbtxt").c_str()), 0);
  EXPECT_FALSE(joshua::mcp::ResolveImageSource(root_, "alias.pbtxt", "sensor:webcam", 21).ok());
  std::ofstream(directory_ + "/candidate.pbtxt", std::ios::app)
      << "robot { perceptions { single_perceptions { sensor_name: \"webcam\" sensor_type: IMAGE } "
         "} }";
  EXPECT_FALSE(joshua::mcp::ResolveImageSource(root_, "candidate.pbtxt", "sensor:webcam", 21).ok());
}
TEST_F(RosSourceBindingTest, RejectsIndistinguishableSensorsOnSharedImagePublisher) {
  std::ofstream output(directory_ + "/candidate.pbtxt");
  output << "robot { perceptions { ";
  for (const auto* name : {"first", "second"})
    output << "single_perceptions { sensor_name: \"" << name
           << "\" sensor_type: IMAGE node { id: 12 node_type: CAMERA_PUBLISHER "
           << "publishers { ros2_data_type: IMAGE topic: \"/shared\" } } } ";
  output << "} }";
  output.close();
  EXPECT_FALSE(joshua::mcp::ResolveImageSource(root_, "candidate.pbtxt", "sensor:first", 0).ok());
}
TEST_F(RosSourceBindingTest, DoesNotRequireOpenCvOrSpecificPublisherImplementation) {
  std::string config =
      "robot { perceptions { single_perceptions { sensor_name: \"generic\" "
      "sensor_type: IMAGE node { id: 7 node_type: LIDAR_PUBLISHER publishers { "
      "ros2_data_type: IMAGE topic: \"/external/image\" } } } } }";
  std::ofstream(directory_ + "/candidate.pbtxt") << config;
  const auto result =
      joshua::mcp::ResolveImageSource(root_, "candidate.pbtxt", "sensor:generic", 0);
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(result->node, "lidar_publisher_node_7");
}
}  // namespace
