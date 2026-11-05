#include "ros2/camera_publisher.h"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"

namespace {
using namespace std::chrono_literals;

struct FakeCameraState {
  bool available = false;
  bool configured = false;
  bool fail_next_read = false;
  int initializations = 0;
  int successful_initializations = 0;
  int reads = 0;
  int teardowns = 0;
  int uninitialized_reads = 0;
  absl::Status configuration_status;
};

class FakeCamera : public robot::perception::PerceptionInterface {
 public:
  explicit FakeCamera(std::shared_ptr<FakeCameraState> state) : state_(std::move(state)) {}

  absl::Status Init() override {
    ++state_->initializations;
    if (!state_->configuration_status.ok()) return state_->configuration_status;
    if (!state_->available) return absl::UnavailableError("Camera temporarily busy");
    state_->configured = true;
    ++state_->successful_initializations;
    return absl::OkStatus();
  }

  std::string GetId() override {
    return "fake_camera";
  }

  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override {
    ++state_->reads;
    if (!state_->configured) {
      ++state_->uninitialized_reads;
      return absl::FailedPreconditionError("Read before configured initialization");
    }
    if (state_->fail_next_read) {
      state_->fail_next_read = false;
      return absl::InternalError("Camera disconnected");
    }
    robot::perception::PerceptionPacket packet;
    packet.set_perception_id(GetId());
    auto* image = packet.mutable_image();
    image->set_width(1);
    image->set_height(1);
    image->set_channels(3);
    image->set_encoding("bgr8");
    const char data[] = {static_cast<char>(state_->successful_initializations), '\x02', '\x03'};
    image->set_data(data, 3);
    return packet;
  }

  absl::Status Teardown() override {
    ++state_->teardowns;
    state_->configured = false;
    return absl::OkStatus();
  }

 private:
  std::shared_ptr<FakeCameraState> state_;
};

class CameraPublisherTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    ASSERT_EQ(setenv("ROS_DOMAIN_ID", "156", 1), 0);
    ASSERT_EQ(setenv("ROS_LOCALHOST_ONLY", "1", 1), 0);
    const char* temporary = std::getenv("TEST_TMPDIR");
    ASSERT_NE(temporary, nullptr);
    ASSERT_EQ(setenv("ROS_LOG_DIR", temporary, 1), 0);
    rclcpp::init(0, nullptr);
  }

  static void TearDownTestSuite() {
    rclcpp::shutdown();
  }

  void SetUp() override {
    auto* camera = config_.mutable_robot()->mutable_perceptions()->add_single_perceptions();
    camera->set_sensor_name("fake_camera");
    camera->set_sensor_type(robot::perception::IMAGE);
    camera->mutable_opencv_config()->set_fourcc("MJPG");
    camera->mutable_node()->set_id(1);
    camera->mutable_node()->set_node_type(ros2::node::CAMERA_PUBLISHER);
    auto* publisher = camera->mutable_node()->add_publishers();
    publisher->set_topic("camera_recovery_test/image");
    publisher->set_ros2_data_type(ros2::data_type::IMAGE);
    publisher->set_publish_rate_hz(100);
    observer_ = std::make_shared<rclcpp::Node>("camera_recovery_observer");
    subscription_ = observer_->create_subscription<sensor_msgs::msg::Image>(
        publisher->topic(), 10, [this](sensor_msgs::msg::Image::ConstSharedPtr message) {
          images_.push_back(*message);
        });
    executor_.add_node(observer_);
  }

  void TearDown() override {
    if (publisher_) executor_.remove_node(publisher_);
    executor_.remove_node(observer_);
    publisher_.reset();
    subscription_.reset();
    observer_.reset();
  }

  void Start() {
    publisher_ = std::make_shared<CameraPublisher>(
        "camera_recovery_publisher",
        1,
        config_,
        [state = state_](const auto&)
            -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
          return std::make_unique<FakeCamera>(state);
        },
        [this]() { return now_; });
    executor_.add_node(publisher_);
  }

  bool WaitUntil(const std::function<bool()>& predicate) {
    const auto deadline = CameraPublisher::Clock::now() + 3s;
    while (!predicate() && CameraPublisher::Clock::now() < deadline) {
      executor_.spin_once(5ms);
    }
    return predicate();
  }

  void SpinFor(std::chrono::milliseconds duration) {
    const auto deadline = CameraPublisher::Clock::now() + duration;
    while (CameraPublisher::Clock::now() < deadline) executor_.spin_once(5ms);
  }

  config::Config config_;
  std::shared_ptr<FakeCameraState> state_ = std::make_shared<FakeCameraState>();
  CameraPublisher::Clock::time_point now_{};
  std::shared_ptr<CameraPublisher> publisher_;
  std::shared_ptr<rclcpp::Node> observer_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  std::vector<sensor_msgs::msg::Image> images_;
};

TEST_F(CameraPublisherTest, RetainsUnavailableCameraAndPublishesAfterInitializationRecovers) {
  Start();
  EXPECT_EQ(state_->initializations, 1);
  EXPECT_EQ(state_->teardowns, 1);
  ASSERT_TRUE(
      WaitUntil([&]() { return observer_->count_publishers("camera_recovery_test/image") == 1; }));

  state_->available = true;
  SpinFor(50ms);
  EXPECT_EQ(state_->initializations, 1);
  EXPECT_EQ(state_->reads, 0);
  EXPECT_TRUE(images_.empty());

  now_ += 1s;
  ASSERT_TRUE(WaitUntil([&]() { return !images_.empty(); }));
  EXPECT_EQ(state_->initializations, 2);
  EXPECT_EQ(state_->uninitialized_reads, 0);
  ASSERT_EQ(images_.front().data.size(), 3);
  EXPECT_EQ(images_.front().data, (std::vector<uint8_t>{3, 2, 1}));
  EXPECT_EQ(images_.front().encoding, "rgb8");
  SpinFor(30ms);
  EXPECT_EQ(state_->initializations, 2);
}

TEST_F(CameraPublisherTest, RepeatedOpenFailuresUseOneSecondBackoff) {
  Start();
  now_ += 1s;
  ASSERT_TRUE(WaitUntil([&]() { return state_->initializations == 2; }));
  state_->available = true;
  now_ += 500ms;
  SpinFor(50ms);
  EXPECT_EQ(state_->initializations, 2);
  EXPECT_EQ(state_->reads, 0);
  now_ += 500ms;
  ASSERT_TRUE(WaitUntil([&]() { return !images_.empty(); }));
  EXPECT_EQ(state_->initializations, 3);
  EXPECT_EQ(state_->uninitialized_reads, 0);
}

TEST_F(CameraPublisherTest, ReadFailureClosesCameraAndReinitializesBeforeResuming) {
  state_->available = true;
  Start();
  ASSERT_TRUE(WaitUntil([&]() { return !images_.empty(); }));
  state_->fail_next_read = true;
  ASSERT_TRUE(WaitUntil([&]() { return state_->teardowns == 1; }));
  EXPECT_FALSE(state_->configured);
  const auto failed_reads = state_->reads;
  SpinFor(50ms);
  EXPECT_EQ(state_->reads, failed_reads);
  EXPECT_EQ(state_->initializations, 1);

  now_ += 1s;
  // A session marker distinguishes recovered frames from previously queued DDS messages.
  ASSERT_TRUE(WaitUntil([&]() { return !images_.empty() && images_.back().data.at(2) == 2; }));
  EXPECT_EQ(state_->initializations, 2);
  EXPECT_TRUE(state_->configured);
  EXPECT_EQ(state_->uninitialized_reads, 0);
}

TEST_F(CameraPublisherTest, InvalidConfigurationFailsStartupInsteadOfRetrying) {
  state_->configuration_status = absl::InvalidArgumentError("Invalid camera FourCC");
  EXPECT_THROW(Start(), std::invalid_argument);
  EXPECT_EQ(state_->initializations, 1);
  EXPECT_EQ(state_->reads, 0);
  EXPECT_EQ(state_->teardowns, 1);
}
}  // namespace
