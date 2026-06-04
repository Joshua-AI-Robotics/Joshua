#include <list>
#include <memory>
#include <thread>
#include <vector>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/action/factory/action_factory.h"
#include "robot/action/proto/action_packet.pb.h"
#include "ros2/node_runner.h"
#include "ros2/proto/ros2_data_type.pb.h"
#include "ros2/utils/packet_parser.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/float32.hpp"

class ActionSubscriber : public rclcpp::Node {
 private:
  struct Actuator {
    std::string topic;
    std::shared_ptr<robot::action::ActionInterface> interface;
    std::pair<float, float> limits;
    bool normalized;
    std::string device_id;
    rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr subscription;
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
      const auto& qos_setting = single_action.node().qos_setting();
      const std::string device_id = action_proto.actuator_name();

      auto interface = robot::action::ActionFactory::CreateAction(single_action);
      if (!interface.ok()) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to create action interface for actuator '%s'. Check hardware "
                     "connection or permissions.",
                     action_proto.actuator_name().c_str());
        continue;
      }

      auto shared_interface =
          std::shared_ptr<robot::action::ActionInterface>(std::move(interface.value()));

      robot::action::ActionPacket enable_packet;
      enable_packet.set_preset(robot::action::PresetCommand::PRESET_ENABLE_TORQUE);
      if (!shared_interface->SetAction(enable_packet).ok()) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to enable torque for actuator '%s'!",
                     action_proto.actuator_name().c_str());
        continue;
      }

      for (const auto& subscription : single_action.node().subscriptions()) {
        const auto data_type = subscription.ros2_data_type();
        const std::string& topic = subscription.topic();

        if (data_type != ros2::data_type::FLOAT32) {
          RCLCPP_ERROR(
              this->get_logger(),
              "Unsupported ros2_data_type %d for actuator '%s'. Only FLOAT32 is supported.",
              static_cast<int>(data_type),
              topic.c_str());
          continue;
        }

        auto topic_device_id = ros2_utils::DeviceIdFromTopic(topic);
        if (!topic_device_id.ok()) {
          RCLCPP_ERROR(this->get_logger(),
                       "Invalid actuator Float32 topic '%s': %s",
                       topic.c_str(),
                       topic_device_id.status().message().data());
          continue;
        }
        if (topic_device_id.value() != device_id) {
          RCLCPP_ERROR(this->get_logger(),
                       "Actuator topic '%s' device_id '%s' does not match actuator_name '%s'.",
                       topic.c_str(),
                       topic_device_id.value().c_str(),
                       device_id.c_str());
          continue;
        }

        Actuator& actuator =
            actuators_.emplace_back(Actuator{.topic = topic,
                                             .interface = shared_interface,
                                             .limits = {action_proto.operational_lower_limit(),
                                                        action_proto.operational_upper_limit()},
                                             .normalized = subscription.normalized(),
                                             .device_id = device_id});

        const auto qos = ros2_utils::CreateQosSetting(qos_setting);
        actuator.subscription = this->create_subscription<std_msgs::msg::Float32>(
            topic, qos, [this, &actuator](const std_msgs::msg::Float32::ConstSharedPtr msg) {
              auto parsed =
                  ros2_utils::ActionPacketFromFloat(msg->data, actuator.topic, actuator.normalized);
              if (!parsed.ok()) {
                RCLCPP_ERROR(this->get_logger(),
                             "Failed to parse Float32 payload for '%s': %s",
                             actuator.topic.c_str(),
                             parsed.status().message().data());
                return;
              }
              actuator.reusable_packet = parsed.value();
              const auto [lower, upper] = actuator.limits;
              ros2_utils::DenormalizeActionPacket(actuator.reusable_packet, lower, upper);
              if (!actuator.interface->SetAction(actuator.reusable_packet).ok()) {
                RCLCPP_ERROR(this->get_logger(),
                             "Failed to set action for actuator '%s'!",
                             actuator.topic.c_str());
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

    for (auto& actuator : actuators_) {
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
