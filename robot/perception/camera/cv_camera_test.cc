#include "robot/perception/camera/cv_camera.h"

#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {
bool capture_open = false;
bool allow_open = false;
std::vector<int> numeric_opens;
std::vector<std::string> path_opens;
std::vector<int> backends;
}  // namespace

// Interpose the OpenCV capture entrypoints in this Linux test executable.
// No camera is opened, and production code needs no test-only capture wrapper.
namespace cv {
bool VideoCapture::open(int index, int backend) {
  numeric_opens.push_back(index);
  backends.push_back(backend);
  capture_open = allow_open;
  return capture_open;
}
bool VideoCapture::open(const String& path, int backend) {
  path_opens.push_back(path);
  backends.push_back(backend);
  capture_open = allow_open;
  return capture_open;
}
bool VideoCapture::isOpened() const {
  return capture_open;
}
void VideoCapture::release() {
  capture_open = false;
}
bool VideoCapture::set(int, double) {
  return true;
}
double VideoCapture::get(int) const {
  return 640;
}
}  // namespace cv

namespace robot::perception {
namespace {
class CameraDeviceSelectionTest : public ::testing::Test {
 protected:
  void SetUp() override {
    capture_open = false;
    allow_open = false;
    numeric_opens.clear();
    path_opens.clear();
    backends.clear();
    config_.set_sensor_type(IMAGE);
    config_.set_sensor_name("camera");
    config_.mutable_opencv_config()->set_id(7);
    config_.mutable_opencv_config()->set_fourcc("MJPG");
    config_.mutable_opencv_config()->set_width(640);
    config_.mutable_opencv_config()->set_height(480);
    config_.mutable_opencv_config()->set_fps(30);
  }
  SinglePerception config_;
};

TEST_F(CameraDeviceSelectionTest, EmptyPathKeepsNumericIndex) {
  CvCamera camera(config_);
  EXPECT_FALSE(camera.Init().ok());
  EXPECT_EQ(numeric_opens, (std::vector<int>{7, 7, 7}));
  EXPECT_TRUE(path_opens.empty());
  EXPECT_EQ(backends, (std::vector<int>{cv::CAP_V4L2, cv::CAP_V4L2, cv::CAP_V4L2}));
}

TEST_F(CameraDeviceSelectionTest, MissingPathNeverFallsBackToNumericIndex) {
  const std::string path = "/dev/v4l/by-id/test-camera-video-index0";
  config_.mutable_opencv_config()->set_device_path(path);
  CvCamera camera(config_);
  const auto status = camera.Init();
  EXPECT_FALSE(status.ok());
  EXPECT_NE(std::string(status.message()).find(path), std::string::npos);
  EXPECT_EQ(path_opens, (std::vector<std::string>{path, path, path}));
  EXPECT_TRUE(numeric_opens.empty());
  EXPECT_EQ(backends, (std::vector<int>{cv::CAP_V4L2, cv::CAP_V4L2, cv::CAP_V4L2}));
}

TEST_F(CameraDeviceSelectionTest, ReconnectRetriesTheSamePath) {
  const std::string path = "/dev/v4l/by-path/test-port-video-index0";
  config_.mutable_opencv_config()->set_device_path(path);
  allow_open = true;
  CvCamera camera(config_);
  ASSERT_TRUE(camera.Init().ok());
  // Simulate loss of the capture device before the next acquisition.
  capture_open = false;
  allow_open = false;
  EXPECT_FALSE(camera.GetData().ok());
  EXPECT_EQ(path_opens, (std::vector<std::string>{path, path, path, path}));
  EXPECT_TRUE(numeric_opens.empty());
}
}  // namespace
}  // namespace robot::perception
