#include <chrono>
#include <list>
#include <thread>
#include <variant>

#include "absl/strings/escaping.h"
#include "config/proto/config.pb.h"
#include "google/protobuf/text_format.h"
#include "rclcpp/rclcpp.hpp"
#include "robot/action/factory/action_factory.h"
#include "robot/action/proto/action_packet.pb.h"
#include "ros2/node_runner.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/float32.hpp"
#include "std_msgs/msg/string.hpp"

using SubscriptionVariant = std::variant<rclcpp::Subscription<std_msgs::msg::Float32>::SharedPtr,
                                         rclcpp::Subscription<std_msgs::msg::String>::SharedPtr>;

class ActionSubscriber : public rclcpp::Node {
 private:
  struct Actuator {
    std::string topic;
    std::unique_ptr<robot::action::ActionInterface> interface;
    std::pair<float, float> limits;
    robot::perception::EncoderDataMode encoder_data_mode;
    float offset = 0.0f;
    float multiplier = 1.0f;
    float pre_shift = 0.0f;
    bool mapping_valid = true;
    SubscriptionVariant subscription;
    robot::action::ActionPacket reusable_packet;
  };

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
          Actuator& actuator = actuators_.emplace_back(
              Actuator{.topic = subscription.topic(),
                       .interface = std::move(interface.value()),
                       .limits = {action_proto.operational_lower_limit(),
                                  action_proto.operational_upper_limit()},
                       .encoder_data_mode = action_proto.encoder_data_mode()});

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
          if (data_type == ros2::data_type::STRING) {
            actuator.subscription = this->create_subscription<std_msgs::msg::String>(
                actuator.topic,
                ros2_utils::CreateQosSetting(qos_setting),
                [this, &actuator](const std_msgs::msg::String::ConstSharedPtr msg) {
                  actuator.reusable_packet.Clear();
                  std::string decoded;
                  bool parsed = false;
                  if (absl::Base64Unescape(msg->data, &decoded)) {
                    parsed = actuator.reusable_packet.ParseFromString(decoded);
                  }
                  if (!parsed) {
                    parsed = google::protobuf::TextFormat::ParseFromString(
                        msg->data, &actuator.reusable_packet);
                  }
                  if (!parsed) {
                    RCLCPP_ERROR(this->get_logger(),
                                 "Failed to parse ActionPacket for '%s'!",
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
            ComputeMapping(actuator);
            actuator.subscription = this->create_subscription<std_msgs::msg::Float32>(
                actuator.topic,
                ros2_utils::CreateQosSetting(qos_setting),
                [this, &actuator](const std_msgs::msg::Float32::ConstSharedPtr msg) {
                  if (!actuator.mapping_valid) {
                    RCLCPP_WARN(this->get_logger(),
                                "Invalid encoder data mode for actuator '%s'!",
                                actuator.topic.c_str());
                    return;
                  }
                  const float action_value = msg->data;
                  const float mapped_position =
                      actuator.offset + (action_value + actuator.pre_shift) * actuator.multiplier;
                  actuator.reusable_packet.Clear();
                  actuator.reusable_packet.set_position(mapped_position);
                  if (!actuator.interface->SetAction(actuator.reusable_packet).ok()) {
                    RCLCPP_ERROR(this->get_logger(),
                                 "Failed to set action for actuator '%s'!",
                                 actuator.topic.c_str());
                  }
                });
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

  void ComputeMapping(Actuator& actuator) {
    const float range = actuator.limits.second - actuator.limits.first;
    switch (actuator.encoder_data_mode) {
      case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_RAW:
        actuator.offset = 0.0f;
        actuator.pre_shift = 0.0f;
        actuator.multiplier = 1.0f;
        actuator.mapping_valid = true;
        return;
      case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_NORMALIZED_ZERO_TO_ONE:
        actuator.offset = actuator.limits.first;
        actuator.pre_shift = 0.0f;
        actuator.multiplier = range;
        actuator.mapping_valid = true;
        return;
      case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_NORMALIZED_MINUS_ONE_TO_ONE:
        actuator.offset = actuator.limits.first;
        actuator.pre_shift = 1.0f;
        actuator.multiplier = range / 2.0f;
        actuator.mapping_valid = true;
        return;
      case robot::perception::EncoderDataMode::ENCODER_DATA_MODE_NORMALIZED_RADIAN:
        actuator.offset = actuator.limits.first;
        actuator.pre_shift = static_cast<float>(M_PI) / 2.0f;
        actuator.multiplier = range / static_cast<float>(M_PI);
        actuator.mapping_valid = true;
        return;
      default:
        actuator.mapping_valid = false;
        RCLCPP_WARN(this->get_logger(),
                    "Invalid encoder data mode for actuator '%s'!",
                    actuator.topic.c_str());
        return;
    }
  }
};

int main(int argc, char* argv[]) {
  return ros2_utils::RunNode<ActionSubscriber>(argc, argv, "actuator_subscriber");
}
