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
#include "ros2/utils/mapped_message.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"

class ActionSubscriber : public rclcpp::Node {
 private:
  struct Actuator {
    std::string topic;
    std::shared_ptr<robot::action::ActionInterface> interface;
    std::pair<float, float> limits;
    bool normalized;
    std::string device_id;
    rclcpp::SubscriptionBase::SharedPtr subscription;
    robot::action::ActionPacket reusable_packet;
  };

 public:
  ActionSubscriber(const std::string& node_name, const int node_id, const config::Config& config)
      : Node(node_name) {
    const auto validation = config::ValidateConfig(config);
    if (!validation.ok()) throw std::invalid_argument(validation.ToString());
    std::vector<std::shared_ptr<ros2_utils::MappedMessage>> codecs;
    for (const auto& action : config.robot().actions().single_actions()) {
      if (action.action_type() != robot::action::ACTUATOR ||
          action.node().id() != static_cast<uint32_t>(node_id))
        continue;
      for (const auto& sub : action.node().subscriptions()) {
        auto codec =
            ros2_utils::MappedMessage::Create(sub.ros2_data_type(), sub.scalar_mapping(), false);
        if (!codec.ok())
          throw std::invalid_argument(sub.topic() + ": " + codec.status().ToString());
        codecs.push_back(*codec);
      }
    }
    size_t endpoint = 0;
    for (const auto& single_action : config.robot().actions().single_actions()) {
      if (single_action.action_type() != robot::action::ActionType::ACTUATOR ||
          static_cast<int>(single_action.node().id()) != node_id) {
        continue;
      }

      const auto& action_proto = single_action.actuator();
      const auto& qos_setting = single_action.node().qos_setting();
      const std::string device_id = action_proto.actuator_name();

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
        auto codec = codecs.at(endpoint++);
        // Explicit commands decouple externally owned topic names from devices.
        const std::string command_topic =
            subscription.command().empty() ? topic : device_id + "/" + subscription.command();
        Actuator& actuator =
            actuators_.emplace_back(Actuator{.topic = topic,
                                             .interface = shared_interface,
                                             .limits = {action_proto.operational_lower_limit(),
                                                        action_proto.operational_upper_limit()},
                                             .normalized = subscription.normalized(),
                                             .device_id = device_id});

        const auto qos = ros2_utils::CreateQosSetting(qos_setting);
        actuator.subscription = this->create_generic_subscription(
            topic,
            codec->type_name(),
            qos,
            [this, &actuator, codec, command_topic](
                std::shared_ptr<rclcpp::SerializedMessage> msg) {
              auto value = codec->Decode(*msg);
              if (!value.ok()) {
                RCLCPP_ERROR(get_logger(),
                             "Invalid command on '%s': %s",
                             actuator.topic.c_str(),
                             value.status().ToString().c_str());
                return;
              }
              auto parsed =
                  ros2_utils::ActionPacketFromFloat(*value, command_topic, actuator.normalized);
              if (!parsed.ok()) {
                RCLCPP_ERROR(
                    get_logger(), "Invalid command: %s", parsed.status().ToString().c_str());
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

#ifndef JOSHUA_NODE_TEST
int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<ActionSubscriber>(argc, argv, "actuator_subscriber");
}

#else
std::shared_ptr<rclcpp::Node> MakeActuatorSubscriberForTest(const config::Config& config) {
  return std::make_shared<ActionSubscriber>("actuator_command_test", 1, config);
}
#endif
