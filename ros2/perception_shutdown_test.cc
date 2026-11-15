// A self-executing test child keeps ROS threads and intentionally stuck reads out
// of the process that owns NodeManager and its fork/guardian lifecycle.
#define JOSHUA_NODE_TEST
#include <signal.h>

#include <cstdlib>
#include <filesystem>
#include <future>

#include "gtest/gtest.h"
#include "launcher/node_manager/node_manager.h"
#include "ros2/camera_publisher.cc"

namespace {
using namespace std::chrono_literals;
class StuckCamera : public robot::perception::PerceptionInterface {
 public:
  absl::Status Init() override {
    return absl::OkStatus();
  }
  std::string GetId() override {
    return "stuck";
  }
  absl::Status Teardown() override {
    return absl::OkStatus();
  }
  absl::StatusOr<robot::perception::PerceptionPacket> GetData() override {
    entered.set_value();
    // Model a backend that cannot interrupt capture, including on stop request.
    std::unique_lock lock(mutex);
    wake.wait(lock, [] { return false; });
    return absl::CancelledError("unreachable");
  }
  std::promise<void> entered;
  std::mutex mutex;
  std::condition_variable wake;
};

int RunStuckCamera(int argc, char** argv) {
  return joshua::RunGuarded("stuck camera fixture", [&] {
    setenv("ROS_DOMAIN_ID", "160", 1);
    setenv("ROS_LOCALHOST_ONLY", "1", 1);
    setenv("ROS_LOG_DIR", std::getenv("TEST_TMPDIR"), 1);
    rclcpp::init(argc, argv);
    rclcpp::on_shutdown([] { joshua::RequestShutdown(); });
    // Force the final escalation step rather than letting SIGTERM mask a stuck join.
    signal(SIGTERM, SIG_IGN);
    config::Config config;
    auto* camera = config.mutable_robot()->mutable_perceptions()->add_single_perceptions();
    camera->set_sensor_type(robot::perception::IMAGE);
    camera->set_sensor_name("stuck");
    camera->mutable_node()->set_id(1);
    auto* output = camera->mutable_node()->add_publishers();
    output->set_topic("stuck/image");
    output->set_ros2_data_type(ros2::data_type::IMAGE);
    output->set_publish_rate_hz(30);
    auto driver = std::make_unique<StuckCamera>();
    auto entered = driver->entered.get_future();
    auto node = std::make_shared<CameraPublisher>(
        "stuck_camera",
        1,
        config,
        [&](const auto&)
            -> absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>> {
          return std::move(driver);
        });
    if (entered.wait_for(2s) != std::future_status::ready) _exit(2);
    joshua::ThrowIfError(joshua::NotifyReady());
    while (!joshua::ShutdownRequested()) std::this_thread::sleep_for(5ms);
    node.reset();  // Correctly retains ownership and joins; the parent must escalate.
    return 0;
  });
}

TEST(PerceptionShutdownTest, UninterruptibleCaptureIsKilledAndReapedByExistingManager) {
  launcher::NodeManager manager;
  launcher::NodeManager::LaunchSpec spec;
  spec.name = "stuck_camera";
  spec.executable = std::filesystem::read_symlink("/proc/self/exe").string();
  spec.arguments = {"--stuck-camera-child"};
  ASSERT_TRUE(manager.StartProcess(spec).ok());
  ASSERT_TRUE(manager.AwaitReady(5s).ok());
  const auto start = std::chrono::steady_clock::now();
  // Forced termination remains visible in the retained process record.
  manager.Shutdown({100ms, 100ms, 1s}).IgnoreError();
  EXPECT_LT(std::chrono::steady_clock::now() - start, 3s);
  ASSERT_EQ(manager.GetNodeStatus().size(), 1u);
  const auto status = manager.GetNodeStatus().front();
  EXPECT_TRUE(status.forced_shutdown);
  EXPECT_TRUE(status.reaped);
  EXPECT_EQ(status.termination_signal, SIGKILL);
  EXPECT_FALSE(manager.HasLiveNodes());
}
}  // namespace

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--stuck-camera-child") return RunStuckCamera(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
