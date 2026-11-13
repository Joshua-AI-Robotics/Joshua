#include <atomic>
#include <chrono>
#include <cstdlib>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "robot/perception/camera/recovering_camera.h"
#include "ros2/observation_publisher.h"

namespace {
using namespace std::chrono_literals;

struct FakeCameraState {
  std::atomic<bool> available{false};
  std::atomic<bool> configured{false};
  std::atomic<bool> fail_next_read{false};
  std::atomic<bool> throw_next_read{false};
  std::atomic<int> initializations{0};
  std::atomic<int> successful_initializations{0};
  std::atomic<int> reads{0};
  std::atomic<int> teardowns{0};
  std::atomic<int> uninitialized_reads{0};
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
    if (state_->throw_next_read) {
      state_->throw_next_read = false;
      throw std::runtime_error("Unexpected camera failure");
    }
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
    packet.set_timestamp(123.125);
    auto* image = packet.mutable_image();
    image->set_width(1);
    image->set_height(1);
    image->set_channels(3);
    image->set_encoding("bgr8");
    const char data[] = {
        static_cast<char>(state_->successful_initializations.load()), '\x02', '\x03'};
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
    source_.reset();
    publisher_.reset();
    subscription_.reset();
    observer_.reset();
  }

  using Factory =
      std::function<absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>>(
          const robot::perception::SinglePerception&)>;
  void Start(Factory factory = {}) {
    if (!factory) {
      factory = [state = state_](const auto&)
          -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
        return std::make_unique<FakeCamera>(state);
      };
    }
    publisher_ = std::make_shared<rclcpp::Node>("camera_recovery_publisher");
    const auto sensor = config_.robot().perceptions().single_perceptions(0);
    source_ = std::make_unique<ros2_utils::ObservationPublisher>(
        *publisher_,
        sensor,
        config_.robot().boards(),
        [this,
         factory,
         sensor]() -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
          auto camera = factory(sensor);
          if (!camera.ok()) return camera.status();
          return std::make_unique<robot::perception::RecoveringCamera>(
              std::move(*camera), [this] { return now_.load(); });
        });
    executor_.add_node(publisher_);
  }

  bool WaitUntil(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      executor_.spin_once(5ms);
    }
    return predicate();
  }

  void SpinFor(std::chrono::milliseconds duration) {
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) executor_.spin_once(5ms);
  }

  config::Config config_;
  std::shared_ptr<FakeCameraState> state_ = std::make_shared<FakeCameraState>();
  std::atomic<double> now_{1'700'000'000.0};
  std::shared_ptr<rclcpp::Node> publisher_;
  std::unique_ptr<ros2_utils::ObservationPublisher> source_;
  std::shared_ptr<rclcpp::Node> observer_;
  rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr subscription_;
  rclcpp::executors::SingleThreadedExecutor executor_;
  std::vector<sensor_msgs::msg::Image> images_;
};

TEST_F(CameraPublisherTest, RetainsUnavailableCameraAndPublishesAfterInitializationRecovers) {
  Start();
  ASSERT_TRUE(WaitUntil([&] { return state_->teardowns == 1; }));
  EXPECT_EQ(state_->initializations, 1);
  EXPECT_EQ(state_->teardowns, 1);
  ASSERT_TRUE(
      WaitUntil([&]() { return observer_->count_publishers("camera_recovery_test/image") == 1; }));

  state_->available = true;
  SpinFor(50ms);
  EXPECT_EQ(state_->initializations, 1);
  EXPECT_EQ(state_->reads, 0);
  EXPECT_TRUE(images_.empty());

  now_ = now_ + 1.0;
  ASSERT_TRUE(WaitUntil([&]() { return !images_.empty(); }));
  EXPECT_EQ(state_->initializations, 2);
  EXPECT_EQ(state_->uninitialized_reads, 0);
  ASSERT_EQ(images_.front().data.size(), 3);
  EXPECT_EQ(images_.front().data, (std::vector<uint8_t>{3, 2, 1}));
  EXPECT_EQ(images_.front().encoding, "rgb8");
  EXPECT_EQ(images_.front().header.stamp.sec, static_cast<int32_t>(joshua::kRobotStartTime) + 123);
  EXPECT_EQ(images_.front().header.stamp.nanosec, 125'000'000u);
  SpinFor(30ms);
  EXPECT_EQ(state_->initializations, 2);
}

TEST_F(CameraPublisherTest, RepeatedOpenFailuresUseOneSecondBackoff) {
  Start();
  ASSERT_TRUE(WaitUntil([&] { return state_->teardowns == 1; }));
  now_ = now_ + 1.0;
  ASSERT_TRUE(WaitUntil([&]() { return state_->teardowns == 2; }));
  state_->available = true;
  now_ = now_ + 0.5;
  SpinFor(50ms);
  EXPECT_EQ(state_->initializations, 2);
  EXPECT_EQ(state_->reads, 0);
  now_ = now_ + 0.5;
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
  const auto failed_reads = state_->reads.load();
  SpinFor(50ms);
  EXPECT_EQ(state_->reads, failed_reads);
  EXPECT_EQ(state_->initializations, 1);

  now_ = now_ + 1.0;
  // A session marker distinguishes recovered frames from previously queued DDS messages.
  ASSERT_TRUE(WaitUntil([&]() { return !images_.empty() && images_.back().data.at(2) == 2; }));
  EXPECT_EQ(state_->initializations, 2);
  EXPECT_TRUE(state_->configured);
  EXPECT_EQ(state_->uninitialized_reads, 0);
}

TEST_F(CameraPublisherTest, InvalidConfigurationIsReportedWithoutRetrying) {
  state_->configuration_status = absl::InvalidArgumentError("Invalid camera pixel format");
  std::string health;
  auto subscription = observer_->create_subscription<std_msgs::msg::String>(
      "camera_recovery_test/image/health", 10, [&](std_msgs::msg::String::ConstSharedPtr message) {
        health = message->data;
      });
  Start();
  ASSERT_TRUE(
      WaitUntil([&] { return health.find("Invalid camera pixel format") != std::string::npos; }));
  now_ = now_ + 10.0;
  SpinFor(50ms);
  EXPECT_EQ(state_->initializations, 1);
  EXPECT_EQ(state_->reads, 0);
  EXPECT_EQ(state_->teardowns, 1);
  EXPECT_TRUE(images_.empty());
}

TEST_F(CameraPublisherTest, MultipleTopicsShareOneAcquisitionAndInitialization) {
  auto* sensor = config_.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  auto second = sensor->node().publishers(0);
  second.set_topic("camera_recovery_test/second_image");
  *sensor->mutable_node()->add_publishers() = second;
  std::vector<sensor_msgs::msg::Image> other_images;
  auto subscription = observer_->create_subscription<sensor_msgs::msg::Image>(
      second.topic(), 10, [&](sensor_msgs::msg::Image::ConstSharedPtr message) {
        other_images.push_back(*message);
      });
  state_->available = true;
  Start();
  ASSERT_TRUE(WaitUntil([&] { return !images_.empty() && !other_images.empty(); }));
  EXPECT_EQ(state_->initializations, 1);
  EXPECT_EQ(state_->uninitialized_reads, 0);
  EXPECT_EQ(images_.back().data, other_images.back().data);
  EXPECT_EQ(images_.back().header.frame_id, "camera_frame");
}

TEST_F(CameraPublisherTest, UnexpectedCaptureExceptionIsVisibleInObservationHealth) {
  std::string health;
  auto subscription = observer_->create_subscription<std_msgs::msg::String>(
      "camera_recovery_test/image/health", 10, [&](std_msgs::msg::String::ConstSharedPtr message) {
        health = message->data;
      });
  state_->available = true;
  state_->throw_next_read = true;
  Start();
  ASSERT_TRUE(
      WaitUntil([&] { return health.find("Unexpected camera failure") != std::string::npos; }));
  EXPECT_TRUE(images_.empty());
  source_.reset();
  EXPECT_EQ(state_->teardowns, 1);
}

TEST_F(CameraPublisherTest, FastPublicationDoesNotIncreaseAnotherCamerasAcquisitionRate) {
  auto slow_sensor = config_.robot().perceptions().single_perceptions(0);
  slow_sensor.set_sensor_name("slow_camera");
  slow_sensor.mutable_node()->mutable_publishers(0)->set_topic("camera_recovery_test/slow_image");
  slow_sensor.mutable_node()->mutable_publishers(0)->set_publish_rate_hz(1);
  auto slow_state = std::make_shared<FakeCameraState>();
  slow_state->available = true;
  state_->available = true;
  Start();
  ros2_utils::ObservationPublisher slow(
      *publisher_,
      slow_sensor,
      config_.robot().boards(),
      [slow_state]() -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
        return std::make_unique<robot::perception::RecoveringCamera>(
            std::make_unique<FakeCamera>(slow_state));
      });
  ASSERT_TRUE(WaitUntil([&] { return slow_state->reads > 0 && state_->reads > 5; }));
  EXPECT_EQ(slow_state->reads, 1);
  EXPECT_EQ(slow_state->initializations, 1);
}
}  // namespace
