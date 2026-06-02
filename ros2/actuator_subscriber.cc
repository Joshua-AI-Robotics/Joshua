#include <chrono>
#include <list>
#include <thread>
#include <variant>

#include "config/proto/config.pb.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/action/factory/action_factory.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/perception/proto/perception_packet.pb.h"
#include "ros2/node_runner.h"
#include "ros2/proto/node.pb.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/u_int8_multi_array.hpp"

using SubscriptionVariant =
    std::variant<rclcpp::Subscription<std_msgs::msg::UInt8MultiArray>::SharedPtr>;

class ActionSubscriber : public rclcpp::Node {
 private:
  struct Actuator {
    std::string topic;
    std::unique_ptr<robot::action::ActionInterface> interface;
    ros2::node::PayloadType payload_type;
    SubscriptionVariant subscription;
    robot::action::ActionPacket reusable_packet;
  };

  static bool ParseSubscriptionPayload(const Actuator& actuator,
                                       const std_msgs::msg::UInt8MultiArray& msg,
                                       robot::action::ActionPacket& packet) {
    const auto* data = msg.data.data();
    const int size = static_cast<int>(msg.data.size());
    switch (actuator.payload_type) {
      case ros2::node::PAYLOAD_TYPE_UNSPECIFIED:
      case ros2::node::PAYLOAD_TYPE_ACTION_PACKET:
        return packet.ParseFromArray(data, size);
      case ros2::node::PAYLOAD_TYPE_PERCEPTION_PACKET: {
        robot::perception::PerceptionPacket perception;
        if (!perception.ParseFromArray(data, size)) {
          return false;
        }
        if (!perception.has_position()) {
          return false;
        }
        packet.Clear();
        packet.set_position(perception.position().position());
        if (perception.timestamp_ns() != 0) {
          packet.set_timestamp_ns(perception.timestamp_ns());
        }
        return true;
      }
      default:
        return false;
    }
  }

 public:
  ActionSubscriber(const std::string& node_name, const int node_id, const config::Config& config)
      : Node(node_name) {
    for (const auto& single_action : config.robot().actions().single_actions()) {
      if (single_action.action_type() == robot::action::ActionType::ACTUATOR &&
          static_cast<int>(single_action.node().id()) == node_id) {
        const auto& action_proto = single_action.actuator();
        const auto& qos_setting = single_action.node().qos_setting();

        auto interface = robot::action::ActionFactory::CreateAction(single_action);
        if (!interface.ok()) {
          RCLCPP_ERROR(this->get_logger(),
                       "Failed to create action interface for actuator '%s'. Check hardware "
                       "connection or permissions.",
                       action_proto.actuator_name().c_str());
          continue;
        }

        for (const auto& subscription : single_action.node().subscriptions()) {
          Actuator& actuator =
              actuators_.emplace_back(Actuator{.topic = subscription.topic(),
                                               .interface = std::move(interface.value()),
                                               .payload_type = subscription.payload_type()});

          actuator.reusable_packet.Clear();
          actuator.reusable_packet.set_preset(robot::action::PresetCommand::PRESET_ENABLE_TORQUE);
          auto status = actuator.interface->SetAction(actuator.reusable_packet);
          if (!status.ok()) {
            RCLCPP_ERROR(this->get_logger(),
                         "Failed to enable torque for actuator '%s'!",
                         actuator.topic.c_str());
            continue;
          }

          const auto data_type = subscription.ros2_data_type();
          if (data_type == ros2::data_type::UINT8_MULTI_ARRAY) {
            actuator.subscription = this->create_subscription<std_msgs::msg::UInt8MultiArray>(
                actuator.topic,
                ros2_utils::CreateQosSetting(qos_setting),
                [this, &actuator](const std_msgs::msg::UInt8MultiArray::ConstSharedPtr msg) {
                  actuator.reusable_packet.Clear();
                  if (!ParseSubscriptionPayload(actuator, *msg, actuator.reusable_packet)) {
                    RCLCPP_ERROR(this->get_logger(),
                                 "Failed to parse subscription payload for '%s'!",
                                 actuator.topic.c_str());
                    return;
                  }
                  if (!actuator.interface->SetAction(actuator.reusable_packet).ok()) {
                    RCLCPP_ERROR(this->get_logger(),
                                 "Failed to set action for actuator '%s'!",
                                 actuator.topic.c_str());
                  }
                });
          } else {
            RCLCPP_ERROR(this->get_logger(),
                         "Unsupported ros2_data_type %d for actuator '%s'. "
                         "Only UINT8_MULTI_ARRAY is supported.",
                         static_cast<int>(data_type),
                         actuator.topic.c_str());
          }
        }
      }
    }

    if (actuators_.empty()) {
      RCLCPP_ERROR(
          this->get_logger(), "No actuators found in configuration for node_id %d!", node_id);
      return;
    }

    RCLCPP_INFO(this->get_logger(),
                "Actuator subscriber node started with %zu actuators for node_id %d!",
                actuators_.size(),
                node_id);
  }

  ~ActionSubscriber() {
    std::vector<std::thread> threads;

    for (auto& actuator : actuators_) {
      threads.emplace_back([&actuator]() {
        actuator.reusable_packet.Clear();
        actuator.reusable_packet.set_preset(robot::action::PresetCommand::PRESET_TEARDOWN);
        auto status = actuator.interface->SetAction(actuator.reusable_packet);
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
