#pragma once

#include <list>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/action/factory/action_factory.h"
#include "robot/action/interfaces/control_gate.h"
#include "robot/action/proto/action_packet.pb.h"
#include "ros2/logging.h"
#include "ros2/node_runner.h"
#include "ros2/utils/packet_parser.h"
#include "std_srvs/srv/trigger.hpp"

class ActionSubscriber : public rclcpp::Node {
  struct ActuatorSubscription {
    std::shared_ptr<robot::action::ControlGate> gate;
    rclcpp::SubscriptionBase::SharedPtr subscription;
  };

 public:
  ActionSubscriber(const std::string& name,
                   int id,
                   const config::Config& config,
                   bool standalone = true)
      : Node(name), standalone_(standalone) {
    ros2_utils::SetLogNodeName(get_logger().get_name());
    control_group_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
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
      gates_.push_back(gate);
      const auto& a = action.actuator();
      for (const auto& sub : action.node().subscriptions()) {
        auto result = ros2_utils::CreateActionMessageSubscription(
            *this,
            sub,
            action,
            [this,
             gate,
             source = sub.source_id().empty() ? "default" : sub.source_id(),
             lower = a.operational_lower_limit(),
             upper =
                 a.operational_upper_limit()](absl::StatusOr<robot::action::ActionPacket> packet) {
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
          for (auto& gate : gates_)
            gate->Poll(robot::action::ControlGate::Clock::now()).IgnoreError();
        },
        control_group_);
  }
  ~ActionSubscriber() override {
    watchdog_.reset();
    if (standalone_) ShutdownActions().IgnoreError();
  }

 protected:
  absl::Status StopActions() {
    absl::Status result;
    for (auto& gate : gates_) result.Update(gate->Stop());
    return result;
  }
  absl::Status ShutdownActions() {
    absl::Status result;
    for (auto& gate : gates_) result.Update(gate->Shutdown());
    return result;
  }

 private:
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
  bool standalone_;
  std::vector<std::shared_ptr<robot::action::ControlGate>> gates_;
  std::list<ActuatorSubscription> subscriptions_;
  std::vector<rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr> services_;
  rclcpp::CallbackGroup::SharedPtr control_group_;
  rclcpp::TimerBase::SharedPtr watchdog_;
};
