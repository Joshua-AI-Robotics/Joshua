#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/board/factory/board_factory.h"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/logging.h"
#include "ros2/node_runner.h"
#include "ros2/observation_publisher.h"
#include "ros2/utils/actuator_commands.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"
#include "std_srvs/srv/trigger.hpp"
#include "utils/scope_exit.h"
#include "utils/status_macros.h"

class BoardNode : public rclcpp::Node {
 public:
  using Factory =
      std::function<absl::StatusOr<std::unique_ptr<robot::perception::PerceptionInterface>>(
          const robot::perception::SinglePerception&)>;
  using WorkerStart = std::function<std::thread(std::string, std::function<void()>)>;
  BoardNode(const std::string& name,
            int id,
            const config::Config& config,
            Factory factory = {},
            WorkerStart start_worker = joshua::StartWorker)
      : Node(name) {
    ros2_utils::SetLogNodeName(get_logger().get_name());
    bool complete = false;
    joshua::ScopeExit rollback([&] {
      if (!complete) Shutdown();
    });
    if (!factory)
      factory = [&config](const auto& sensor) {
        return robot::perception::PerceptionFactory::CreatePerception(sensor,
                                                                      config.robot().boards());
      };
    commands_ = std::make_unique<ros2_utils::ActuatorCommands>(*this, id, config);
    std::set<std::string> names;
    for (const auto& a : config.robot().actions().single_actions())
      if (a.node().id() == static_cast<uint32_t>(id)) names.insert(a.actuator().board_name());
    for (const auto& s : config.robot().perceptions().single_perceptions()) {
      if (s.node().id() != static_cast<uint32_t>(id)) continue;
      names.insert(s.has_sts3215_encoder_config() ? s.sts3215_encoder_config().board_name()
                                                  : s.joshua_wire_scalar_config().board_name());
      if (s.node().publishers().empty()) continue;
      auto source = std::make_unique<Source>(s,
                                             s.has_sts3215_encoder_config() ? "sts3215_tick" : "",
                                             s.observation_policy().frame_id());
      auto* record = source.get();
      record->cached = s.has_joshua_wire_scalar_config();
      ros2_utils::AddObservationOutputs(
          *this,
          s,
          [record] {
            if (record->cached && record->driver && !record->stopping && record->initialized) {
              auto result = record->driver->GetData();
              record->state.Record(std::move(result), Clock::now());
            }
            return record->state.Get();
          },
          MakePublisher,
          record->timers);
      auto made = factory(s);
      if (made.ok())
        record->driver = std::move(*made);
      else
        record->state.Record(made.status(), Clock::now());
      observations_.push_back(std::move(source));
    }
    for (const auto& b : config.robot().boards()) {
      if (!names.count(b.name())) continue;
      auto board = robot::board::BoardFactory::GetOrCreate(b);
      if (!board.ok()) throw std::runtime_error(board.status().ToString());
      boards_.emplace(b.name(), *board);
    }
    stop_group_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    stop_ = create_service<std_srvs::srv::Trigger>(
        "~/estop",
        [this](std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> reply) {
          const auto requests = commands_->RequestStopActions();
          reply->success = true;
          // Each board performs its own fallback before waiting on other boards.
          // Keep the bus-wide requests pending until all stop work has finished.
          std::map<std::string, std::future<absl::Status>> stops;
          for (const auto& [name, board] : boards_)
            stops.emplace(name, std::async(std::launch::async, [this, name, board] {
                            auto status = board->EmergencyStop();
                            const auto disabled = commands_->StopBoardActions(name, status.ok());
                            if (absl::IsUnimplemented(status)) status = absl::OkStatus();
                            status.Update(disabled);
                            return status;
                          }));
          for (auto& [name, stop] : stops) {
            const auto status = stop.get();
            if (!status.ok()) {
              reply->success = false;
              reply->message += status.ToString() + "; ";
            }
          }
          const auto completed = ros2_utils::ActuatorCommands::CompleteStopActions(requests);
          if (!completed.ok()) {
            reply->success = false;
            reply->message += completed.ToString() + "; ";
          }
          if (reply->success)
            reply->message =
                "Host stop interlocked; available board stop acknowledged; physical verification "
                "unsupported";
        },
        rmw_qos_profile_services_default,
        stop_group_);
    for (auto& source : observations_) {
      if (!source->driver) continue;
      if (source->cached) {
        const auto status = source->driver->Init();
        source->initialized = status.ok();
        if (!status.ok()) source->state.Record(status, Clock::now());
      } else {
        source->worker = start_worker("device feedback acquisition",
                                      [record = source.get()] { Acquire(*record); });
      }
    }
    complete = true;
  }
  ~BoardNode() override {
    Shutdown();
  }

 private:
  friend class BoardNodeStopTest;
  friend class BoardNodeTest;
  using Clock = robot::perception::ObservationState::Clock;
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
    bool cached = false;
    bool initialized = false;
    ~Source() {
      RequestStop();
      if (worker.joinable()) worker.join();
      if (cached && driver)
        joshua::RunCleanup("scalar subscription cleanup",
                           [&] { joshua::ThrowIfError(driver->Teardown()); });
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
  };
  static void Acquire(Source& source) {
    joshua::ScopeExit cleanup([&] {
      joshua::RunCleanup("sensor teardown",
                         [&] { joshua::ThrowIfError(source.driver->Teardown()); });
    });
    if (source.stopping || joshua::ShutdownRequested()) return;
    const auto status = source.driver->Init();
    if (!status.ok()) {
      source.state.Record(status, Clock::now());
      return;
    }
    while (!source.stopping && !joshua::ShutdownRequested()) {
      const auto start = Clock::now();
      auto result = source.driver->GetData();
      if (source.stopping || joshua::ShutdownRequested()) break;
      source.state.Record(std::move(result), Clock::now());
      std::unique_lock lock(source.wait_mutex);
      source.wake.wait_until(lock, start + source.period, [&] { return source.stopping.load(); });
    }
  }
  static ros2_utils::ObservationPublish MakePublisher(
      rclcpp::Node& node,
      const ros2::node::Publisher& output,
      const robot::perception::SinglePerception& sensor) {
    if (output.observation_packet() || sensor.has_joshua_wire_scalar_config())
      return ros2_utils::MakePacketPublisher(node, output, sensor);
    auto made = ros2_utils::CreatePositionPacketPublisher(node, output, sensor);
    if (!made.ok()) throw std::runtime_error(made.status().ToString());
    return *made;
  }
  void Shutdown() noexcept {
    for (auto& source : observations_) source->RequestStop();
    commands_.reset();
    observations_.clear();
    for (auto& [name, board] : boards_)
      joshua::RunCleanup("device board teardown", [&] { joshua::ThrowIfError(board->Teardown()); });
    boards_.clear();
  }
  std::unique_ptr<ros2_utils::ActuatorCommands> commands_;
  std::vector<std::unique_ptr<Source>> observations_;
  std::map<std::string, std::shared_ptr<robot::board::BoardInterface>> boards_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr stop_;
  rclcpp::CallbackGroup::SharedPtr stop_group_;
};

#ifndef JOSHUA_NODE_TEST
JOSHUA_MAIN(argc, argv) {
  return ros2_utils::RunNode<BoardNode>(argc, argv, "board_node", 2);
}
#endif
