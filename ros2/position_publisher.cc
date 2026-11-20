#include <thread>

#include "robot/perception/factory/perception_factory.h"
#include "ros2/acquisition_schedule.h"
#include "ros2/node_runner.h"
#include "ros2/observation_publisher.h"
#include "ros2/utils/robot_time_conversion.h"
#include "utils/scope_exit.h"
#include "utils/status_macros.h"

class PositionPublisher : public rclcpp::Node {
 public:
  using Clock = robot::perception::ObservationState::Clock;
  using Factory =
      std::function<absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>>(
          const robot::perception::SinglePerception&)>;
  using WorkerStart = std::function<std::thread(std::string, std::function<void()>)>;
  PositionPublisher(const std::string& name,
                    int id,
                    const config::Config& config,
                    Factory factory = {},
                    WorkerStart start_worker = joshua::StartWorker)
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
          sensor.sensor_type() != robot::perception::POSITION || sensor.node().publishers().empty())
        continue;
      auto source =
          std::make_unique<Source>(sensor, "sts3215_tick", sensor.observation_policy().frame_id());
      auto* record = source.get();
      ros2_utils::AddObservationOutputs(
          *this, sensor, [record] { return record->state.Get(); }, MakePublisher, record->timers);
      sources_.push_back(std::move(source));
    }
    size_t index = 0;
    for (const auto& sensor : config.robot().perceptions().single_perceptions()) {
      if (sensor.node().id() != static_cast<uint32_t>(id) ||
          sensor.sensor_type() != robot::perception::POSITION || sensor.node().publishers().empty())
        continue;
      auto& source = *sources_.at(index++);
      auto made = factory(sensor);
      if (!made.ok()) {
        source.state.Record(made.status(), Clock::now());
        continue;
      }
      source.driver = std::move(*made);
      source.timers.push_back(source.schedule.CreateTimer(*this, sensor));
    }
    for (auto& source : sources_) {
      if (source->driver)
        source->worker = start_worker("position_publisher acquisition",
                                      [record = source.get()] { Acquire(*record); });
    }
    complete = true;
  }
  ~PositionPublisher() override {
    StopSources();
  }

 private:
  struct Source {
    Source(const robot::perception::SinglePerception& sensor, std::string unit, std::string frame)
        : state(std::chrono::milliseconds(sensor.observation_policy().has_stale_after_ms()
                                              ? sensor.observation_policy().stale_after_ms()
                                              : 1000),
                Clock::now(),
                {std::move(unit), std::move(frame)}) {}
    ~Source() {
      RequestStop();
      if (worker.joinable()) worker.join();
      state.Stop();
    }
    void RequestStop() noexcept {
      schedule.Stop();
      for (auto& timer : timers)
        joshua::RunCleanup("observation timer cancellation", [&] { timer->cancel(); });
      if (driver) driver->RequestStop();
    }
    robot::perception::ObservationState state;
    std::unique_ptr<robot::perception::PerceptionInterface> driver;
    std::vector<rclcpp::TimerBase::SharedPtr> timers;
    ros2_utils::AcquisitionSchedule schedule;
    std::thread worker;
  };
  static void Acquire(Source& source) {
    joshua::ScopeExit cleanup([&] {
      joshua::RunCleanup("sensor teardown",
                         [&] { joshua::ThrowIfError(source.driver->Teardown()); });
    });
    if (source.schedule.stopped() || joshua::ShutdownRequested()) return;
    const auto status = source.driver->Init();
    if (!status.ok()) {
      source.state.Record(status, Clock::now());
      return;
    }
    while (source.schedule.WaitForRead() && !joshua::ShutdownRequested()) {
      auto result = source.driver->GetData();
      if (source.schedule.stopped() || joshua::ShutdownRequested()) break;
      source.state.Record(std::move(result), Clock::now());
    }
  }
  static ros2_utils::ObservationPublish MakePublisher(
      rclcpp::Node& node,
      const ros2::node::Publisher& config,
      const robot::perception::SinglePerception& sensor) {
    if (config.observation_packet()) return ros2_utils::MakePacketPublisher(node, config, sensor);
    auto made = ros2_utils::CreatePositionPacketPublisher(node, config, sensor);
    if (!made.ok()) throw std::runtime_error(made.status().ToString());
    return *made;
  }
  void StopSources() noexcept {
    for (auto& source : sources_) source->RequestStop();
    sources_.clear();
  }
  std::vector<std::unique_ptr<Source>> sources_;
};

#ifndef JOSHUA_NODE_TEST
JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<PositionPublisher>(argc, argv, "position_publisher");
}
#endif
