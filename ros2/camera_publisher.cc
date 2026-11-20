#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <thread>

#include "robot/perception/factory/perception_factory.h"
#include "ros2/node_runner.h"
#include "ros2/observation_publisher.h"
#include "ros2/utils/robot_time_conversion.h"
#include "sensor_msgs/msg/image.hpp"
#include "utils/scope_exit.h"
#include "utils/status_macros.h"

class CameraPublisher : public rclcpp::Node {
 public:
  using Clock = robot::perception::ObservationState::Clock;
  using Factory =
      std::function<absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>>(
          const robot::perception::SinglePerception&)>;
  using WorkerStart = std::function<std::thread(std::string, std::function<void()>)>;
  CameraPublisher(const std::string& name,
                  int id,
                  const config::Config& config,
                  Factory factory = {},
                  WorkerStart start_worker = joshua::StartWorker,
                  std::function<double()> now = joshua::RobotTime)
      : Node(name) {
    ros2_utils::SetLogNodeName(get_logger().get_name());
    if (!factory)
      factory = [&config](const auto& sensor) {
        return robot::perception::PerceptionFactory::CreatePerception(sensor,
                                                                      config.robot().boards());
      };
    bool complete = false;
    joshua::ScopeExit rollback([&] {
      if (!complete) StopSources();
    });
    for (const auto& sensor : config.robot().perceptions().single_perceptions()) {
      if (sensor.node().id() != static_cast<uint32_t>(id) ||
          sensor.sensor_type() != robot::perception::IMAGE || sensor.node().publishers().empty())
        continue;
      auto source = std::make_unique<Source>(sensor,
                                             "pixel",
                                             sensor.observation_policy().frame_id().empty()
                                                 ? "camera_frame"
                                                 : sensor.observation_policy().frame_id());
      auto* record = source.get();
      record->now = now;
      ros2_utils::AddObservationOutputs(
          *this, sensor, [record] { return record->state.Get(); }, MakePublisher, record->timers);
      sources_.push_back(std::move(source));
    }
    size_t index = 0;
    for (const auto& sensor : config.robot().perceptions().single_perceptions()) {
      if (sensor.node().id() != static_cast<uint32_t>(id) ||
          sensor.sensor_type() != robot::perception::IMAGE || sensor.node().publishers().empty())
        continue;
      auto& source = *sources_.at(index++);
      auto made = factory(sensor);
      if (!made.ok()) {
        source.state.Record(made.status(), Clock::now());
        continue;
      }
      source.driver = std::move(*made);
    }
    for (auto& source : sources_) {
      if (source->driver)
        source->worker = start_worker("camera_publisher acquisition",
                                      [record = source.get()] { Acquire(*record); });
    }
    complete = true;
  }
  ~CameraPublisher() override {
    StopSources();
  }

 private:
  struct Source {
    Source(const robot::perception::SinglePerception& sensor, std::string unit, std::string frame)
        : state(std::chrono::milliseconds(sensor.observation_policy().has_stale_after_ms()
                                              ? sensor.observation_policy().stale_after_ms()
                                              : 1000),
                Clock::now(),
                {std::move(unit), std::move(frame)}) {
      uint32_t rate = 1;
      for (const auto& output : sensor.node().publishers())
        rate = std::max(rate, output.publish_rate_hz());
      period = std::chrono::nanoseconds(1000000000ull / rate);
    }
    ~Source() {
      RequestStop();
      if (worker.joinable()) worker.join();
      state.Stop();
    }
    void RequestStop() noexcept {
      stopping.store(true);
      for (auto& timer : timers)
        joshua::RunCleanup("observation timer cancellation", [&] { timer->cancel(); });
      if (driver) driver->RequestStop();
      wake.notify_all();
    }
    robot::perception::ObservationState state;
    std::unique_ptr<robot::perception::PerceptionInterface> driver;
    std::vector<rclcpp::TimerBase::SharedPtr> timers;
    std::atomic<bool> stopping{false};
    std::mutex wait_mutex;
    std::condition_variable wake;
    std::chrono::nanoseconds period;
    std::thread worker;
    std::function<double()> now;
  };
  static void Acquire(Source& source) {
    bool active = false;
    joshua::ScopeExit cleanup([&] {
      if (!active) return;
      joshua::RunCleanup("sensor teardown",
                         [&] { joshua::ThrowIfError(source.driver->Teardown()); });
    });
    if (source.stopping || joshua::ShutdownRequested()) return;
    bool initialized = false;
    double retry_after = 0.0;
    absl::Status last_error = absl::UnavailableError("Camera not initialized");
    while (!source.stopping && !joshua::ShutdownRequested()) {
      const auto start = Clock::now();
      if (!initialized && !absl::IsInvalidArgument(last_error) && source.now() >= retry_after) {
        active = true;
        last_error = source.driver->Init();
        initialized = last_error.ok();
      }
      if (source.stopping || joshua::ShutdownRequested()) break;
      auto result = initialized ? source.driver->GetData()
                                : absl::StatusOr<robot::perception::PerceptionPacket>(last_error);
      if (!result.ok() && active) {
        initialized = false;
        active = false;
        last_error = result.status();
        const auto status = source.driver->Teardown();
        if (!status.ok())
          last_error =
              absl::Status(last_error.code(),
                           std::string(last_error.message()) + "; teardown: " + status.ToString());
        retry_after = source.now() + 1.0;
      }
      if (source.stopping || joshua::ShutdownRequested()) break;
      source.state.Record(std::move(result), Clock::now());
      std::unique_lock lock(source.wait_mutex);
      source.wake.wait_until(lock, start + source.period, [&] { return source.stopping.load(); });
    }
  }
  static ros2_utils::ObservationPublish MakePublisher(
      rclcpp::Node& node,
      const ros2::node::Publisher& config,
      const robot::perception::SinglePerception& sensor) {
    if (config.observation_packet()) return ros2_utils::MakePacketPublisher(node, config, sensor);
    const auto qos = ros2_utils::CreateQosSetting(sensor.node().qos_setting());
    auto publisher = node.create_publisher<sensor_msgs::msg::Image>(config.topic(), qos);
    return [publisher, name = sensor.sensor_name()](const auto& packet) {
      const auto& image = packet.image();
      sensor_msgs::msg::Image msg;
      ABSL_ASSIGN_OR_RETURN(msg.header.stamp, ros2_utils::ToRosTimestamp(packet.timestamp()));
      msg.header.frame_id = packet.sample_metadata().frame_id();
      msg.width = image.width();
      msg.height = image.height();
      msg.encoding = "rgb8";
      msg.step = msg.width * 3;
      msg.data.assign(image.data().begin(), image.data().end());
      for (size_t i = 0; i < msg.data.size(); i += 3) std::swap(msg.data[i], msg.data[i + 2]);
      publisher->publish(msg);
      return absl::OkStatus();
    };
  }
  void StopSources() noexcept {
    for (auto& source : sources_) source->RequestStop();
    sources_.clear();
  }
  std::vector<std::unique_ptr<Source>> sources_;
};

#ifndef JOSHUA_NODE_TEST
JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<CameraPublisher>(argc, argv, "camera_publisher");
}
#endif
