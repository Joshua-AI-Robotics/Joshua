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
  ActionSubscriber(const std::string& node_name, const int node_id, const config::Config& config)
      : Node(node_name) {
    for (const auto& single_action : config.robot().actions().single_actions()) {
      if (single_action.action_type() != robot::action::ActionType::ACTUATOR ||
          static_cast<int>(single_action.node().id()) != node_id) {
        continue;
      }

      const auto& action_proto = single_action.actuator();

      auto interface =
          robot::action::ActionFactory::CreateAction(single_action, config.robot().boards());
      if (!interface.ok()) throw std::runtime_error(interface.status().ToString());

      auto shared_interface =
          std::shared_ptr<robot::action::ActionInterface>(std::move(interface.value()));

      robot::action::ActionPacket enable_packet;
      enable_packet.set_preset(robot::action::PresetCommand::PRESET_ENABLE_TORQUE);
      const auto enabled = shared_interface->SetAction(enable_packet);
      if (!enabled.ok()) throw std::runtime_error(enabled.ToString());

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
                RCLCPP_ERROR(get_logger(),
                             "Invalid command on '%s': %s",
                             actuator.topic.c_str(),
                             parsed.status().ToString().c_str());
                return;
              }
              actuator.reusable_packet = *parsed;
              const auto [lower, upper] = actuator.limits;
              ros2_utils::DenormalizeActionPacket(actuator.reusable_packet, lower, upper);
              const auto status = actuator.interface->SetAction(actuator.reusable_packet);
              if (!status.ok()) {
                RCLCPP_ERROR(get_logger(),
                             "Actuator '%s' rejected command: %s",
                             actuator.topic.c_str(),
                             status.ToString().c_str());
              }
            });
        if (!result.ok()) throw std::invalid_argument(result.status().ToString());
        actuator.subscription = *result;
      }
    }

    if (actuators_.empty()) {
      RCLCPP_ERROR(
          this->get_logger(), "No actuators found in configuration for node_id %d!", node_id);
      return;
    }

    RCLCPP_INFO(this->get_logger(),
                "Actuator subscriber node started with %zu subscriptions for node_id %d!",
                actuators_.size(),
                node_id);
  }

  ~ActionSubscriber() {
    std::vector<std::thread> threads;

    std::set<robot::action::ActionInterface*> torn_down;
    for (auto& actuator : actuators_) {
      if (!torn_down.insert(actuator.interface.get()).second) continue;
      threads.emplace_back([&actuator]() {
        robot::action::ActionPacket teardown_packet;
        teardown_packet.set_preset(robot::action::PresetCommand::PRESET_TEARDOWN);
        auto status = actuator.interface->SetAction(teardown_packet);
        if (!status.ok()) {
          RCLCPP_ERROR(get_logger(), "Failed to teardown actuator '%s'", actuator.topic.c_str());
        }
      });
    }

    for (auto& thread : threads) {
      thread.join();
    }
  }

 private:
  std::list<Actuator> actuators_;
};

#ifndef JOSHUA_NODE_TEST
int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<ActionSubscriber>(argc, argv, "actuator_subscriber");
}

#else
std::shared_ptr<rclcpp::Node> MakeActuatorSubscriberForTest(const config::Config& config) {
  return ros2_utils::CreateValidatedNode(
      "actuator_command_test",
      1,
      config,
      [](const std::string& name, int id, const config::Config& validated_config) {
        return std::make_shared<ActionSubscriber>(name, id, validated_config);
      });
}
#endif
