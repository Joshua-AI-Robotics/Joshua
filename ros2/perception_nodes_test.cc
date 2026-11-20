#define JOSHUA_NODE_TEST
#include <cstdlib>
#include <future>

#include "gtest/gtest.h"
#include "ros2/board_node.cc"
#include "ros2/camera_publisher.cc"
#include "ros2/lidar_publisher.cc"
#include "ros2/position_publisher.cc"

namespace {
using namespace std::chrono_literals;
using Kind = robot::perception::SensorType;
using Factory = CameraPublisher::Factory;
using WorkerStart = CameraPublisher::WorkerStart;

struct SensorState {
  std::mutex mutex;
  std::condition_variable wake;
  bool released = false;
  bool block_init = false;
  bool block_read = true;
  bool throw_read = false;
  absl::Status cleanup_status;
  std::atomic<int> inits{0}, reads{0}, stops{0}, cleanups{0}, destroyed{0};
  std::atomic<bool> entered{false}, active{false}, cleanup_during_read{false};
  const uint64_t receipt_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  std::chrono::steady_clock::now().time_since_epoch())
                                  .count();
  std::atomic<bool> usable{true};
};
class Sensor : public robot::perception::PerceptionInterface {
 public:
  Sensor(std::shared_ptr<SensorState> state, Kind kind) : state_(std::move(state)), kind_(kind) {}
  ~Sensor() override {
    ++state_->destroyed;
  }
  std::string GetId() override {
    return "sensor";
  }
  absl::Status Init() override {
    ++state_->inits;
    if (state_->block_init) Wait();
    return absl::OkStatus();
  }
  void RequestStop() noexcept override {
    ++state_->stops;
    {
      std::lock_guard lock(state_->mutex);
      state_->released = true;
    }
    state_->wake.notify_all();
  }
  absl::Status Teardown() override {
    ++state_->cleanups;
    state_->cleanup_during_read = state_->active.load();
    return state_->cleanup_status;
  }
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override {
    ++state_->reads;
    if (state_->throw_read) throw std::runtime_error("unexpected sensor failure");
    if (state_->block_read) Wait();
    robot::perception::PerceptionPacket packet;
    packet.set_perception_id("sensor");
    packet.set_timestamp(123.25);
    if (kind_ == robot::perception::IMAGE) {
      auto* image = packet.mutable_image();
      image->set_width(1);
      image->set_height(1);
      image->set_channels(3);
      image->set_encoding("bgr8");
      image->set_data("abc");
    } else if (kind_ == robot::perception::RANGE_SCAN) {
      auto* cloud = packet.mutable_point_cloud();
      cloud->add_x(1);
      cloud->add_y(2);
      cloud->add_z(3);
    } else if (kind_ == robot::perception::SCALAR) {
      auto* scalar = packet.mutable_scalar();
      scalar->set_boot_id(1);
      scalar->set_session_id(2);
      scalar->set_subscription_id(3);
      scalar->set_acquisition_sequence(4);
      scalar->set_clock(1);
      scalar->set_acquisition_us(1234);
      scalar->set_value(42);
      scalar->set_usable(state_->usable);
      scalar->set_receipt_fresh(state_->usable);
      scalar->set_host_receipt_steady_ns(state_->receipt_ns);
      packet.clear_timestamp();
    } else
      packet.mutable_position()->set_position(42);
    return packet;
  }

 private:
  void Wait() {
    std::unique_lock lock(state_->mutex);
    state_->active = true;
    state_->entered = true;
    state_->wake.wait(lock, [&] { return state_->released; });
    state_->active = false;
  }
  std::shared_ptr<SensorState> state_;
  Kind kind_;
};
class TestClock : public joshua::RobotClock {
 public:
  double Now() const override {
    return now.load();
  }
  std::atomic<double> now{100.0};
};
class PerceptionNodesTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    auto clock = std::make_unique<TestClock>();
    clock_ = clock.get();
    ASSERT_TRUE(joshua::SetGlobalRobotClock(std::move(clock)).ok());
    ASSERT_NE(std::getenv("TEST_TMPDIR"), nullptr);
    setenv("ROS_LOG_DIR", std::getenv("TEST_TMPDIR"), 1);
    setenv("ROS_DOMAIN_ID", "159", 1);
    setenv("ROS_LOCALHOST_ONLY", "1", 1);
    rclcpp::init(0, nullptr);
  }
  static void TearDownTestSuite() {
    rclcpp::shutdown();
  }
  void SetUp() override {
    clock_->now = 100.0;
    EXPECT_EQ(joshua::RunGuarded("reset sensor test", [] { return 0; }), 0);
  }
  config::Config Config(Kind kind, int sensors = 1) {
    config::Config config;
    for (int i = 0; i < sensors; ++i) {
      auto* sensor = config.mutable_robot()->mutable_perceptions()->add_single_perceptions();
      sensor->set_sensor_name("sensor_" + std::to_string(i));
      sensor->set_sensor_type(kind);
      sensor->mutable_node()->set_id(1);
      sensor->mutable_observation_policy()->set_stale_after_ms(100);
      auto* output = sensor->mutable_node()->add_publishers();
      output->set_topic("sensor_test/sensor_" + std::to_string(i));
      output->set_ros2_data_type(ros2::data_type::UINT8_MULTI_ARRAY);
      output->set_observation_packet(true);
      output->set_publish_rate_hz(30);
    }
    return config;
  }
  std::shared_ptr<rclcpp::Node> Make(Kind kind,
                                     const config::Config& config,
                                     Factory factory,
                                     WorkerStart start = joshua::StartWorker) {
    if (kind == robot::perception::IMAGE)
      return std::make_shared<CameraPublisher>("sensor_test", 1, config, factory, start);
    if (kind == robot::perception::RANGE_SCAN)
      return std::make_shared<LidarPublisher>("sensor_test", 1, config, factory, start);
    return std::make_shared<PositionPublisher>("sensor_test", 1, config, factory, start);
  }
  bool Wait(const std::function<bool()>& predicate, rclcpp::Executor* executor = nullptr) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline) {
      if (executor)
        executor->spin_once(5ms);
      else
        std::this_thread::sleep_for(1ms);
    }
    return predicate();
  }
  inline static TestClock* clock_;
};

TEST_F(PerceptionNodesTest, BlockedReadsLeaveHealthResponsiveAndCancelBeforeTeardown) {
  for (auto kind :
       {robot::perception::IMAGE, robot::perception::RANGE_SCAN, robot::perception::POSITION}) {
    SCOPED_TRACE(static_cast<int>(kind));
    auto state = std::make_shared<SensorState>();
    auto node =
        Make(kind,
             Config(kind),
             [=](const auto&)
                 -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
               return std::make_unique<Sensor>(state, kind);
             });
    auto observer = std::make_shared<rclcpp::Node>("health_observer");
    bool stale = false;
    auto sub = observer->create_subscription<std_msgs::msg::UInt8MultiArray>(
        "sensor_test/sensor_0/status",
        10,
        [&](std_msgs::msg::UInt8MultiArray::ConstSharedPtr message) {
          robot::perception::PerceptionPacket packet;
          if (packet.ParseFromArray(message->data.data(), message->data.size()))
            stale =
                packet.observation_status().state() == robot::perception::ObservationStatus::STALE;
        });
    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(observer);
    ASSERT_TRUE(Wait([&] { return state->entered && stale; }, &executor));
    executor.remove_node(node);
    const auto start = std::chrono::steady_clock::now();
    node.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - start, 500ms);
    EXPECT_GT(state->stops, 0);
    EXPECT_EQ(state->cleanups, 1);
    EXPECT_EQ(state->destroyed, 1);
    EXPECT_FALSE(state->cleanup_during_read);
  }
}

TEST_F(PerceptionNodesTest, ShutdownDuringInitializationDoesNotStartARead) {
  for (auto kind :
       {robot::perception::IMAGE, robot::perception::RANGE_SCAN, robot::perception::POSITION}) {
    auto state = std::make_shared<SensorState>();
    state->block_init = true;
    auto node =
        Make(kind,
             Config(kind),
             [=](const auto&)
                 -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
               return std::make_unique<Sensor>(state, kind);
             });
    ASSERT_TRUE(Wait([&] { return state->entered.load(); }));
    node.reset();
    EXPECT_EQ(state->reads, 0);
    EXPECT_EQ(state->cleanups, 1);
    EXPECT_FALSE(state->cleanup_during_read);
  }
}

TEST_F(PerceptionNodesTest, LaterWorkerStartFailureCancelsAndJoinsStartedSensor) {
  for (auto kind :
       {robot::perception::IMAGE, robot::perception::RANGE_SCAN, robot::perception::POSITION}) {
    auto first = std::make_shared<SensorState>(), second = std::make_shared<SensorState>();
    int starts = 0;
    Factory factory = [=](const auto& sensor)
        -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
      return std::make_unique<Sensor>(sensor.sensor_name() == "sensor_0" ? first : second, kind);
    };
    WorkerStart start = [&](std::string name, std::function<void()> work) {
      if (++starts == 2) {
        EXPECT_TRUE(Wait([&] { return first->entered.load(); }));
        throw std::runtime_error("thread creation failed");
      }
      return joshua::StartWorker(std::move(name), std::move(work));
    };
    EXPECT_THROW(Make(kind, Config(kind, 2), factory, start), std::runtime_error);
    EXPECT_EQ(first->cleanups, 1);
    EXPECT_FALSE(first->cleanup_during_read);
    EXPECT_EQ(first->destroyed, 1);
    EXPECT_EQ(second->destroyed, 1);
    EXPECT_EQ(second->inits, 0);
  }
}

TEST_F(PerceptionNodesTest, LaterFactoryExceptionDestroysPreparedRecordsWithoutStartingWorkers) {
  auto state = std::make_shared<SensorState>();
  int starts = 0;
  Factory factory = [=](const auto& sensor)
      -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
    if (sensor.sensor_name() == "sensor_1") throw std::runtime_error("construction failed");
    return std::make_unique<Sensor>(state, robot::perception::POSITION);
  };
  EXPECT_THROW(Make(robot::perception::POSITION,
                    Config(robot::perception::POSITION, 2),
                    factory,
                    [&](std::string name, std::function<void()> work) {
                      ++starts;
                      return joshua::StartWorker(name, work);
                    }),
               std::runtime_error);
  EXPECT_EQ(starts, 0);
  EXPECT_EQ(state->inits, 0);
  EXPECT_EQ(state->destroyed, 1);
}

TEST_F(PerceptionNodesTest, TeardownFailureReachesProcessRuntimeOnce) {
  auto state = std::make_shared<SensorState>();
  state->cleanup_status = absl::UnavailableError("cleanup failed");
  const int result = joshua::RunGuarded("sensor cleanup", [&] {
    auto node =
        Make(robot::perception::POSITION,
             Config(robot::perception::POSITION),
             [=](const auto&)
                 -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
               return std::make_unique<Sensor>(state, robot::perception::POSITION);
             });
    EXPECT_TRUE(Wait([&] { return state->entered.load(); }));
    node.reset();
    return 0;
  });
  EXPECT_EQ(result, 1);
  EXPECT_EQ(state->cleanups, 1);
}

TEST_F(PerceptionNodesTest, PausedRobotTimeDoesNotPauseAcquisitionOrCreateWorkersPerOutput) {
  auto state = std::make_shared<SensorState>();
  state->block_read = false;
  auto config = Config(robot::perception::POSITION);
  auto* sensor = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  sensor->mutable_node()->mutable_publishers(0)->set_publish_rate_hz(10);
  auto* second = sensor->mutable_node()->add_publishers();
  *second = sensor->node().publishers(0);
  second->set_topic("sensor_test/fast");
  second->set_publish_rate_hz(30);
  auto ignored = *sensor;
  ignored.mutable_node()->set_id(2);
  *config.mutable_robot()->mutable_perceptions()->add_single_perceptions() = ignored;
  int starts = 0, factories = 0;
  auto node = Make(
      robot::perception::POSITION,
      config,
      [&](const auto&) -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
        ++factories;
        return std::make_unique<Sensor>(state, robot::perception::POSITION);
      },
      [&](std::string name, std::function<void()> work) {
        ++starts;
        return joshua::StartWorker(name, work);
      });
  ASSERT_TRUE(Wait([&] { return state->reads >= 3; }));
  EXPECT_EQ(starts, 1);
  EXPECT_EQ(factories, 1);
  EXPECT_EQ(state->inits, 1);
  auto observer = std::make_shared<rclcpp::Node>("packet_observer");
  int packets = 0;
  auto sub = observer->create_subscription<std_msgs::msg::UInt8MultiArray>(
      "sensor_test/fast", 10, [&](std_msgs::msg::UInt8MultiArray::ConstSharedPtr) { ++packets; });
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(observer);
  ASSERT_TRUE(Wait([&] { return observer->count_publishers("sensor_test/fast") == 1; }, &executor));
  for (int i = 0; i < 10; ++i) executor.spin_once(5ms);
  EXPECT_EQ(packets, 0);
  clock_->now = 100.1;
  ASSERT_TRUE(Wait([&] { return packets == 1; }, &executor));
  for (int i = 0; i < 10; ++i) executor.spin_once(5ms);
  EXPECT_EQ(packets, 1);
  clock_->now = 90.0;
  for (int i = 0; i < 10; ++i) executor.spin_once(5ms);
  EXPECT_EQ(packets, 1);
  clock_->now = 200.0;
  ASSERT_TRUE(Wait([&] { return packets == 2; }, &executor));
  for (int i = 0; i < 10; ++i) executor.spin_once(5ms);
  EXPECT_EQ(packets, 2);
  executor.remove_node(node);
}

TEST_F(PerceptionNodesTest, ScalarCacheHasNoWorkerAndHealthRefreshesWhileRobotTimeIsPaused) {
  auto state = std::make_shared<SensorState>();
  state->block_read = false;
  auto config = Config(robot::perception::SCALAR);
  auto* sensor = config.mutable_robot()->mutable_perceptions()->mutable_single_perceptions(0);
  sensor->mutable_joshua_wire_scalar_config()->set_board_name("test_cache");
  auto second = sensor->node().publishers(0);
  second.set_topic("sensor_test/second");
  *sensor->mutable_node()->add_publishers() = second;
  int workers = 0;
  auto node = std::make_shared<BoardNode>(
      "cached_sensor_test",
      1,
      config,
      [=](const auto&) -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
        return std::make_unique<Sensor>(state, robot::perception::SCALAR);
      },
      [&](std::string name, std::function<void()> work) {
        ++workers;
        return joshua::StartWorker(name, work);
      });
  auto observer = std::make_shared<rclcpp::Node>("cache_observer");
  robot::perception::PerceptionPacket first, other, health;
  auto receive = [](robot::perception::PerceptionPacket& into) {
    return [&into](std_msgs::msg::UInt8MultiArray::ConstSharedPtr message) {
      EXPECT_TRUE(into.ParseFromArray(message->data.data(), message->data.size()));
    };
  };
  auto a = observer->create_subscription<std_msgs::msg::UInt8MultiArray>(
      "sensor_test/sensor_0", 10, receive(first));
  auto b = observer->create_subscription<std_msgs::msg::UInt8MultiArray>(
      "sensor_test/second", 10, receive(other));
  auto status = observer->create_subscription<std_msgs::msg::UInt8MultiArray>(
      "sensor_test/sensor_0/status", 10, receive(health));
  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(node);
  executor.add_node(observer);
  ASSERT_TRUE(Wait([&] { return health.has_observation_status(); }, &executor));
  EXPECT_DOUBLE_EQ(clock_->Now(), 100.0);
  ASSERT_TRUE(Wait([&] { return first.has_scalar() && other.has_scalar(); }, &executor));
  EXPECT_EQ(first.sample_metadata().SerializeAsString(),
            other.sample_metadata().SerializeAsString());
  EXPECT_EQ(first.scalar().host_receipt_steady_ns(), state->receipt_ns);
  EXPECT_EQ(first.sample_metadata().host_receipt_steady_ns(), state->receipt_ns);
  EXPECT_EQ(first.timestamp(), 0);
  const auto original = first.sample_metadata().SerializeAsString();
  state->usable = false;
  ASSERT_TRUE(Wait(
      [&] {
        return health.observation_status().state() == robot::perception::ObservationStatus::INVALID;
      },
      &executor));
  EXPECT_EQ(health.observation_status().valid_samples(), 1);
  EXPECT_EQ(health.sample_metadata().SerializeAsString(), original);
  EXPECT_EQ(workers, 0);
  EXPECT_EQ(state->inits, 1);
  executor.remove_node(node);
  node.reset();
  EXPECT_EQ(state->cleanups, 1);
}
}  // namespace
