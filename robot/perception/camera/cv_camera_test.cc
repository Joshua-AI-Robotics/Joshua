#include "robot/perception/camera/cv_camera.h"

#include <map>

#include "gtest/gtest.h"
namespace robot::perception {
namespace {
class FakeCapture : public CameraCapture {
 public:
  bool opened = false, empty = false, false_read = false, throw_read = false, throw_set = false,
       fail_open = false, unexpected_read = false, unexpected_set = false;
  std::vector<int> order;
  int opens = 0, releases = 0;
  std::map<int, double> settings;
  bool Open(int, int) override {
    ++opens;
    opened = !fail_open;
    return opened;
  }
  bool IsOpened() const override {
    return opened;
  }
  bool Set(int key, double value) override {
    if (unexpected_set) throw std::runtime_error("unexpected configure failure");
    if (throw_set)
      throw cv::Exception(cv::Error::StsError, "configure failed", "Set", __FILE__, __LINE__);
    order.push_back(key);
    settings[key] = value;
    return true;
  }
  double Get(int key) const override {
    return settings.at(key);
  }
  bool Read(cv::Mat& frame) override {
    if (unexpected_read) throw std::runtime_error("unexpected capture failure");
    if (throw_read)
      throw cv::Exception(cv::Error::StsError, "capture failed", "Read", __FILE__, __LINE__);
    if (!empty) frame = cv::Mat(2, 4, CV_8UC3, cv::Scalar(1, 2, 3)).clone();
    return !empty && !false_read;
  }
  void Release() override {
    opened = false;
    ++releases;
  }
};
SinglePerception Config() {
  SinglePerception c;
  c.set_sensor_name("webcam");
  auto* o = c.mutable_opencv_config();
  o->set_width(640);
  o->set_height(480);
  o->set_fps(30);
  o->set_fourcc("MJPG");
  return c;
}
TEST(CvCameraTest, GetDataConfiguresFirstOpenWithoutInitAndReportsActualFrame) {
  auto fake = std::make_unique<FakeCapture>();
  auto* f = fake.get();
  CvCamera camera(Config(), std::move(fake));
  auto packet = camera.GetData();
  ASSERT_TRUE(packet.ok()) << packet.status();
  EXPECT_EQ(f->settings.at(cv::CAP_PROP_FRAME_WIDTH), 640);
  EXPECT_EQ(f->settings.at(cv::CAP_PROP_FRAME_HEIGHT), 480);
  EXPECT_EQ(f->settings.at(cv::CAP_PROP_FPS), 30);
  EXPECT_EQ(f->settings.at(cv::CAP_PROP_FOURCC), cv::VideoWriter::fourcc('M', 'J', 'P', 'G'));
  EXPECT_EQ(f->settings.at(cv::CAP_PROP_BUFFERSIZE), 2);
  EXPECT_EQ(packet->image().width(), 4);
  EXPECT_EQ(packet->image().height(), 2);
}
TEST(CvCameraTest, EmptyCaptureResetsAndNextReadReconfigures) {
  auto fake = std::make_unique<FakeCapture>();
  auto* f = fake.get();
  CvCamera camera(Config(), std::move(fake));
  ASSERT_TRUE(camera.GetData().ok());
  f->empty = true;
  EXPECT_FALSE(camera.GetData().ok());
  EXPECT_FALSE(f->opened);
  f->empty = false;
  f->settings.clear();
  ASSERT_TRUE(camera.GetData().ok());
  EXPECT_EQ(f->opens, 2);
  EXPECT_EQ(f->settings.at(cv::CAP_PROP_FPS), 30);
}
TEST(CvCameraTest, InvalidRuntimeOptionsNeverOpenDevice) {
  for (const auto& fourcc :
       {std::string(), std::string("bad"), std::string("12345"), std::string("a\nbc")}) {
    auto c = Config();
    c.mutable_opencv_config()->set_fourcc(fourcc);
    auto fake = std::make_unique<FakeCapture>();
    auto* f = fake.get();
    CvCamera camera(c, std::move(fake));
    EXPECT_FALSE(camera.Init().ok());
    EXPECT_EQ(f->opens, 0);
  }
  auto c = Config();
  c.mutable_opencv_config()->set_fps(0);
  auto fake = std::make_unique<FakeCapture>();
  auto* f = fake.get();
  CvCamera camera(c, std::move(fake));
  EXPECT_FALSE(camera.GetData().ok());
  EXPECT_EQ(f->opens, 0);
}
TEST(CvCameraTest, FalseReadWithPixelsAndThrowingReadBothResetForRecovery) {
  auto fake = std::make_unique<FakeCapture>();
  auto* f = fake.get();
  CvCamera camera(Config(), std::move(fake));
  f->false_read = true;
  EXPECT_FALSE(camera.GetData().ok());
  EXPECT_FALSE(f->opened);
  f->false_read = false;
  ASSERT_TRUE(camera.GetData().ok());
  f->throw_read = true;
  EXPECT_FALSE(camera.GetData().ok());
  EXPECT_FALSE(f->opened);
  f->throw_read = false;
  EXPECT_TRUE(camera.GetData().ok());
  EXPECT_EQ(f->opens, 3);
}
TEST(CvCameraTest, ExplicitFormatPrecedesModeSettingsAndNullCaptureIsSafe) {
  auto c = Config();
  c.mutable_opencv_config()->set_fourcc("MJPG");
  auto fake = std::make_unique<FakeCapture>();
  auto* f = fake.get();
  CvCamera camera(c, std::move(fake));
  ASSERT_TRUE(camera.Init().ok());
  EXPECT_EQ(f->order.front(), cv::CAP_PROP_FOURCC);
  CvCamera absent(c, nullptr);
  EXPECT_FALSE(absent.Init().ok());
  EXPECT_TRUE(absent.Teardown().ok());
}

TEST(CvCameraTest, InitConfigurationExceptionResetsBeforeLaterRecovery) {
  auto fake = std::make_unique<FakeCapture>();
  auto* f = fake.get();
  CvCamera camera(Config(), std::move(fake));
  f->throw_set = true;
  EXPECT_FALSE(camera.Init().ok());
  EXPECT_FALSE(f->opened);
  f->throw_set = false;
  ASSERT_TRUE(camera.GetData().ok());
  EXPECT_EQ(f->opens, 2);
  EXPECT_EQ(f->settings.at(cv::CAP_PROP_FPS), 30);
}

TEST(CvCameraTest, UnexpectedConfigurationExceptionPropagatesToRuntimeBoundary) {
  auto fake = std::make_unique<FakeCapture>();
  auto* f = fake.get();
  CvCamera camera(Config(), std::move(fake));
  f->unexpected_set = true;
  EXPECT_THROW(camera.Init(), std::runtime_error);
}

TEST(CvCameraTest, UnexpectedCaptureExceptionPropagatesToRuntimeBoundary) {
  auto fake = std::make_unique<FakeCapture>();
  auto* f = fake.get();
  CvCamera camera(Config(), std::move(fake));
  ASSERT_TRUE(camera.Init().ok());
  f->unexpected_read = true;
  EXPECT_THROW(camera.GetData(), std::runtime_error);
}

TEST(CvCameraTest, FailedOpenHasFiniteAttemptsAndLaterReturningBackendRecovers) {
  auto fake = std::make_unique<FakeCapture>();
  auto* f = fake.get();
  CvCamera camera(Config(), std::move(fake));
  f->fail_open = true;
  EXPECT_FALSE(camera.GetData().ok());
  EXPECT_EQ(f->opens, 3);
  f->fail_open = false;
  ASSERT_TRUE(camera.GetData().ok());
  EXPECT_EQ(f->opens, 4);
  EXPECT_EQ(f->settings.at(cv::CAP_PROP_FPS), 30);
}
}  // namespace
}  // namespace robot::perception
