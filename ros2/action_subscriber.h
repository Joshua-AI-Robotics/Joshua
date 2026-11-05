#pragma once

#include <list>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/action/factory/action_factory.h"
#include "robot/action/proto/action_packet.pb.h"
#include "ros2/logging.h"
#include "ros2/node_runner.h"
#include "ros2/proto/ros2_data_type.pb.h"
#include "ros2/utils/packet_parser.h"

class ActionSubscriber : public rclcpp::Node {
 private:
  struct Actuator {
    std::string topic;
    std::shared_ptr<robot::action::ActionInterface> interface;
    std::pair<float, float> limits;
    rclcpp::SubscriptionBase::SharedPtr subscription;
    robot::action::ActionPacket reusable_packet;
  };

 public:
  ActionSubscriber(const std::string& node_name,
                   const int node_id,
                   const config::Config& config,
                   bool auto_enable = true)
      : Node(node_name), manage_lifecycle_(auto_enable) {
    ros2_utils::SetLogNodeName(get_logger().get_name());
    for (const auto& single_action : config.robot().actions().single_actions()) {
      if (single_action.action_type() != robot::action::ActionType::ACTUATOR ||
          static_cast<int>(single_action.node().id()) != node_id) {
        continue;
      }

      const auto& action_proto = single_action.actuator();

      auto interface =
          robot::action::ActionFactory::CreateAction(single_action, config.robot().boards());
      if (!interface.ok()) throw std::runtime_error(interface.status().ToString());

      // Use a shared pointer to ensure that multiple subscriptions can share the same interface.
      auto shared_interface =
          std::shared_ptr<robot::action::ActionInterface>(std::move(interface.value()));

      const bool require_explicit_enable =
          action_proto.motor_type() == robot::action::MOTOR_STEPPER_NEMA17 &&
          action_proto.stepper_config().require_explicit_enable();
      if (auto_enable && !require_explicit_enable) {
        robot::action::ActionPacket enable_packet;
        enable_packet.set_preset(robot::action::PresetCommand::PRESET_ENABLE_TORQUE);
        const auto enabled = shared_interface->SetAction(enable_packet);
        if (!enabled.ok()) throw std::runtime_error(enabled.ToString());
      }

      for (const auto& subscription : single_action.node().subscriptions()) {
        const std::string& topic = subscription.topic();
        Actuator& actuator =
            actuators_.emplace_back(Actuator{.topic = topic,
                                             .interface = shared_interface,
                                             .limits = {action_proto.operational_lower_limit(),
                                                        action_proto.operational_upper_limit()}});

        auto result = ros2_utils::CreateActionMessageSubscription(
            *this,
            subscription,
            single_action,
            [this, &actuator](absl::StatusOr<robot::action::ActionPacket> parsed) {
              if (!parsed.ok()) {
                JOSHUA_LOG(ERROR) << "Invalid command on '" << actuator.topic
                                  << "': " << parsed.status().ToString();
                return;
              }
              actuator.reusable_packet = *parsed;
              const auto [lower, upper] = actuator.limits;
              const auto encoding_status =
                  ros2_utils::ResolvePositionEncoding(actuator.reusable_packet, lower, upper);
              if (!encoding_status.ok()) {
                JOSHUA_LOG(ERROR) << "Invalid position on '" << actuator.topic
                                  << "': " << encoding_status.ToString();
                return;
              }
              const auto status = actuator.interface->SetAction(actuator.reusable_packet);
              if (!status.ok()) {
                JOSHUA_LOG(ERROR) << "Actuator '" << actuator.topic
                                  << "' rejected command: " << status.ToString();
              }
            });
        if (!result.ok()) throw std::invalid_argument(result.status().ToString());
        actuator.subscription = *result;
      }
    }

    if (actuators_.empty()) {
      JOSHUA_LOG(ERROR) << "No actuators found in configuration for node_id " << node_id << "!";
      return;
    }

    JOSHUA_LOG(INFO) << "Actuator subscriber node started with " << actuators_.size()
                     << " subscriptions for node_id " << node_id << "!";
  }

  ~ActionSubscriber() {
    if (!manage_lifecycle_) return;  // Mixed bus owner performs disabled board reset.
    std::vector<std::thread> threads;

    std::set<robot::action::ActionInterface*> torn_down;
    for (auto& actuator : actuators_) {
      if (!torn_down.insert(actuator.interface.get()).second) continue;
      threads.emplace_back([&actuator]() {
        robot::action::ActionPacket teardown_packet;
        teardown_packet.set_preset(robot::action::PresetCommand::PRESET_TEARDOWN);
        auto status = actuator.interface->SetAction(teardown_packet);
        if (!status.ok()) {
          JOSHUA_LOG(ERROR) << "Failed to teardown actuator '" << actuator.topic << "': " << status;
        }
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }
  }

 private:
  bool manage_lifecycle_;
  std::list<Actuator> actuators_;
};
