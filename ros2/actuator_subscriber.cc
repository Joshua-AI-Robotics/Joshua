#include <glog/logging.h>

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

      // Use a shared pointer to ensure that multiple subscriptions can share the same interface.
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
                LOG(ERROR) << "[" << get_name() << "] "
                           << "Invalid command on '" << actuator.topic
                           << "': " << parsed.status().ToString();
                return;
              }
              actuator.reusable_packet = *parsed;
              const auto [lower, upper] = actuator.limits;
              const auto encoding_status =
                  ros2_utils::ResolvePositionEncoding(actuator.reusable_packet, lower, upper);
              if (!encoding_status.ok()) {
                LOG(ERROR) << "[" << get_name() << "] "
                           << "Invalid position on '" << actuator.topic
                           << "': " << encoding_status.ToString();
                return;
              }
              const auto status = actuator.interface->SetAction(actuator.reusable_packet);
              if (!status.ok()) {
                LOG(ERROR) << "[" << get_name() << "] "
                           << "Actuator '" << actuator.topic
                           << "' rejected command: " << status.ToString();
              }
            });
        if (!result.ok()) throw std::invalid_argument(result.status().ToString());
        actuator.subscription = *result;
      }
    }

    if (actuators_.empty()) {
      LOG(ERROR) << "[" << get_name() << "] "
                 << "No actuators found in configuration for node_id " << node_id << "!";
      return;
    }

    LOG(INFO) << "[" << get_name() << "] "
              << "Actuator subscriber node started with " << actuators_.size()
              << " subscriptions for node_id " << node_id << "!";
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
          LOG(ERROR) << "Failed to teardown actuator '" << actuator.topic << "'";
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

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<ActionSubscriber>(argc, argv, "actuator_subscriber");
}
