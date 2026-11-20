#include "ros2/utils/actuator_commands.h"

#include <chrono>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

#include "robot/action/factory/action_factory.h"
#include "ros2/logging.h"
#include "ros2/utils/packet_parser.h"
#include "utils/joshua_runtime.h"
#include "utils/scope_exit.h"
#include "utils/status_macros.h"

namespace ros2_utils {

ActuatorCommands::ActuatorCommands(rclcpp::Node& node, int id, const config::Config& config)
    : node_(node) {
  bool complete = false;
  joshua::ScopeExit rollback([&] {
    if (!complete) ShutdownActions();
  });

  for (const auto& action : config.robot().actions().single_actions()) {
    if (action.action_type() != robot::action::ACTUATOR ||
        action.node().id() != static_cast<uint32_t>(id))
      continue;

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
          node_,
          sub,
          action,
          [this, gate, source, lower, upper](absl::StatusOr<robot::action::ActionPacket> packet) {
            absl::Status status = packet.status();
            if (packet.ok()) {
              status = ros2_utils::ResolvePositionEncoding(*packet, lower, upper);
              if (status.ok())
                status = gate->Execute(source,
                                       *packet,
                                       node_.now().nanoseconds(),
                                       robot::action::ControlGate::Clock::now());
            }
            if (!status.ok()) JOSHUA_LOG(WARNING) << "Command rejected: " << status.ToString();
          });
      if (!result.ok()) throw std::invalid_argument(result.status().ToString());
      subscriptions_.push_back(*result);
    }
    const std::string key =
        a.actuator_name().empty() ? "actuator_" + std::to_string(a.id()) : a.actuator_name();
    const std::string prefix = "~/" + key + "/";
    AddService(prefix + "enable", [this, gate] {
      return gate->Enable(robot::action::ControlGate::Clock::now(), node_.now().nanoseconds());
    });
    AddService(prefix + "stop", [gate] { return gate->Stop(); });
    AddService(prefix + "recover",
               [this, gate] { return gate->Recover(node_.now().nanoseconds()); });
    for (const auto& source : sources)
      AddService(prefix + "select_" + source, [this, gate, source] {
        return gate->SelectSource(source, node_.now().nanoseconds());
      });
  }

  watchdog_ = node_.create_wall_timer(
      std::chrono::milliseconds(10),
      [this] {
        for (auto& actuator : gates_)
          actuator.gate->Poll(robot::action::ControlGate::Clock::now()).IgnoreError();
      },
      node_.get_node_base_interface()->get_default_callback_group());
  complete = true;
}

ActuatorCommands::~ActuatorCommands() {
  ShutdownActions();
}

ActuatorCommands::StopRequests ActuatorCommands::RequestStopActions() {
  StopRequests requests;
  requests.reserve(gates_.size());
  for (auto& actuator : gates_) requests.emplace_back(actuator.gate, actuator.gate->RequestStop());
  return requests;
}

absl::Status ActuatorCommands::CompleteStopActions(const StopRequests& requests) {
  absl::Status result;
  for (const auto& [gate, request] : requests) result.Update(gate->CompleteStopRequest(request));
  return result;
}

absl::Status ActuatorCommands::StopBoardActions(const std::string& board_name,
                                                bool output_already_disabled) {
  absl::Status result;
  for (auto& actuator : gates_)
    if (actuator.board_name == board_name)
      result.Update(actuator.gate->Stop(output_already_disabled));
  return result;
}

void ActuatorCommands::ShutdownActions() {
  watchdog_.reset();
  joshua::RunCleanup("actuator shutdown", [&] {
    RequestStopActions();
    std::vector<std::thread> threads;
    threads.reserve(gates_.size());
    joshua::ScopeExit join([&] {
      for (auto& thread : threads)
        if (thread.joinable()) thread.join();
    });
    for (const auto& actuator : gates_)
      threads.push_back(joshua::StartWorker(
          "actuator teardown", [gate = actuator.gate] { joshua::ThrowIfError(gate->Shutdown()); }));
  });
}

void ActuatorCommands::AddService(const std::string& name, std::function<absl::Status()> action) {
  services_.push_back(node_.create_service<std_srvs::srv::Trigger>(
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
      node_.get_node_base_interface()->get_default_callback_group()));
}

}  // namespace ros2_utils
