// Board-backed observations and actuator commands share this process and factory.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <list>
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
#include "robot/action/factory/action_factory.h"
#include "robot/action/interfaces/control_gate.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/board/factory/board_factory.h"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/logging.h"
#include "ros2/node_runner.h"
#include "ros2/observation_publisher.h"
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
    InitializeActions(id, config);
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
            if (record->cached && record->driver && !record->stopping && record->initialized)
              record->state.Record(record->driver->GetData(), Clock::now());
            return record->state.Get();
          },
          MakePublisher,
          record->timers);
      // Factories resolve shared board handles; sensor workers never own the bus.
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
          const auto requests = RequestStopActions();
          reply->success = true;
          // Each board performs its own fallback before waiting on other boards.
          // Keep the bus-wide requests pending until all stop work has finished.
          std::map<std::string, std::future<absl::Status>> stops;
          for (const auto& [name, board] : boards_)
            stops.emplace(name, std::async(std::launch::async, [this, name, board] {
                            auto status = board->EmergencyStop();
                            const auto disabled = StopBoardActions(name, status.ok());
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
          const auto completed = CompleteStopActions(requests);
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
  struct ControlledActuator {
    std::shared_ptr<robot::action::ControlGate> gate;
    std::string board_name;
  };
  struct ActuatorSubscription {
    std::shared_ptr<robot::action::ControlGate> gate;
    rclcpp::SubscriptionBase::SharedPtr subscription;
  };

  void InitializeActions(int id, const config::Config& config) {
    // Keep all ordinary control work in the subscriptions' mutually exclusive
    // group. Waiting on a busy gate must not occupy the spare ESTOP thread.
    control_group_ = get_node_base_interface()->get_default_callback_group();
    for (const auto& action : config.robot().actions().single_actions()) {
      if (action.action_type() != robot::action::ACTUATOR ||
          action.node().id() != static_cast<uint32_t>(id))
        continue;
      // Driver construction must not enable before the host gate exists.
      auto prepared = action;
      if (prepared.actuator().motor_type() == robot::action::MOTOR_STEPPER_NEMA17)
        prepared.mutable_actuator()->mutable_stepper_config()->set_require_explicit_enable(true);
      auto made = robot::action::ActionFactory::CreateAction(prepared, config.robot().boards());
      if (!made.ok()) throw std::runtime_error(made.status().ToString());
      auto interface = std::shared_ptr<robot::action::ActionInterface>(std::move(*made));
      std::set<std::string> sources;
      for (const auto& sub : action.node().subscriptions())
        sources.insert(sub.source_id().empty() ? "default" : sub.source_id());
      if (sources.empty()) sources.insert("default");
      auto gate = std::make_shared<robot::action::ControlGate>(
          interface, action.actuator().control_policy(), sources);
      const auto initialized = gate->Initialize();
      if (!initialized.ok()) throw std::runtime_error(initialized.ToString());
      gates_.push_back({gate, action.actuator().board_name()});
      const auto& a = action.actuator();
      for (const auto& sub : action.node().subscriptions()) {
        const auto source = sub.source_id().empty() ? "default" : sub.source_id();
        const auto lower = a.operational_lower_limit();
        const auto upper = a.operational_upper_limit();
        auto result = ros2_utils::CreateActionMessageSubscription(
            *this,
            sub,
            action,
            [this, gate, source, lower, upper](absl::StatusOr<robot::action::ActionPacket> packet) {
              absl::Status status = packet.status();
              if (packet.ok()) {
                status = ros2_utils::ResolvePositionEncoding(*packet, lower, upper);
                if (status.ok())
                  status = gate->Execute(source,
                                         *packet,
                                         now().nanoseconds(),
                                         robot::action::ControlGate::Clock::now());
              }
              if (!status.ok()) JOSHUA_LOG(WARNING) << "Command rejected: " << status.ToString();
            });
        if (!result.ok()) throw std::invalid_argument(result.status().ToString());
        subscriptions_.push_back({gate, *result});
      }
      const std::string key =
          a.actuator_name().empty() ? "actuator_" + std::to_string(a.id()) : a.actuator_name();
      const std::string prefix = "~/" + key + "/";
      AddService(prefix + "enable", [this, gate] {
        return gate->Enable(robot::action::ControlGate::Clock::now(), now().nanoseconds());
      });
      AddService(prefix + "stop", [gate] { return gate->Stop(); });
      AddService(prefix + "recover", [this, gate] { return gate->Recover(now().nanoseconds()); });
      for (const auto& source : sources)
        AddService(prefix + "select_" + source, [this, gate, source] {
          return gate->SelectSource(source, now().nanoseconds());
        });
    }
    // Local command lease is independent of board heartbeats and display consumers.
    watchdog_ = create_wall_timer(
        std::chrono::milliseconds(10),
        [this] {
          for (auto& actuator : gates_)
            actuator.gate->Poll(robot::action::ControlGate::Clock::now()).IgnoreError();
        },
        control_group_);
  }
  using StopRequests =
      std::vector<std::pair<std::shared_ptr<robot::action::ControlGate>, uint64_t>>;
  StopRequests RequestStopActions() {
    StopRequests requests;
    requests.reserve(gates_.size());
    for (auto& actuator : gates_)
      requests.emplace_back(actuator.gate, actuator.gate->RequestStop());
    return requests;
  }
  absl::Status CompleteStopActions(const StopRequests& requests) {
    absl::Status result;
    for (const auto& [gate, request] : requests) result.Update(gate->CompleteStopRequest(request));
    return result;
  }
  absl::Status StopBoardActions(const std::string& board_name, bool output_already_disabled) {
    absl::Status result;
    for (auto& actuator : gates_)
      if (actuator.board_name == board_name)
        result.Update(actuator.gate->Stop(output_already_disabled));
    return result;
  }
  void ShutdownActions() {
    watchdog_.reset();
    if (actions_shutdown_) return;
    actions_shutdown_ = true;
    joshua::RunCleanup("actuator shutdown", [&] {
      RequestStopActions();
      std::vector<std::thread> threads;
      threads.reserve(gates_.size());
      joshua::ScopeExit join([&] {
        for (auto& thread : threads)
          if (thread.joinable()) thread.join();
      });
      for (const auto& actuator : gates_)
        threads.push_back(joshua::StartWorker("actuator teardown", [gate = actuator.gate] {
          joshua::ThrowIfError(gate->Shutdown());
        }));
    });
  }

  void AddService(const std::string& name, std::function<absl::Status()> action) {
    services_.push_back(create_service<std_srvs::srv::Trigger>(
        name,
        [action](std::shared_ptr<std_srvs::srv::Trigger::Request>,
                 std::shared_ptr<std_srvs::srv::Trigger::Response> response) {
          auto status = action();
          response->success = status.ok();
          response->message = status.ok() ? "Host control transition complete; device recovery and "
                                            "physical verification are separate"
                                          : status.ToString();
        },
        rmw_qos_profile_services_default,
        control_group_));
  }
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
    ShutdownActions();
    observations_.clear();
    for (auto& [name, board] : boards_)
      joshua::RunCleanup("device board teardown", [&] { joshua::ThrowIfError(board->Teardown()); });
    boards_.clear();
  }
  bool actions_shutdown_ = false;
  std::vector<ControlledActuator> gates_;
  std::list<ActuatorSubscription> subscriptions_;
  std::vector<rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr> services_;
  rclcpp::CallbackGroup::SharedPtr control_group_;
  rclcpp::TimerBase::SharedPtr watchdog_;
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
