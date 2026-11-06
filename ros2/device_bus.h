#pragma once
// Mixed MCU sources and commands deliberately share this process and factory.
#include "robot/board/factory/board_factory.h"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/action_subscriber.h"
#include "ros2/utils/qos_setting.h"
#include "std_msgs/msg/string.hpp"
#include "std_msgs/msg/u_int8_multi_array.hpp"
#include "std_srvs/srv/trigger.hpp"

class DeviceBus : public ActionSubscriber {
 public:
  DeviceBus(const std::string& name, int id, const config::Config& config)
      : ActionSubscriber(name, id, config, false) {
    std::set<std::string> names;
    for (const auto& a : config.robot().actions().single_actions())
      if (a.node().id() == static_cast<uint32_t>(id)) names.insert(a.actuator().board_name());
    for (const auto& s : config.robot().perceptions().single_perceptions()) {
      if (s.node().id() != static_cast<uint32_t>(id)) continue;
      names.insert(s.has_sts3215_encoder_config() ? s.sts3215_encoder_config().board_name()
                                                  : s.joshua_wire_scalar_config().board_name());
      auto made =
          robot::perception::PerceptionFactory::CreatePerception(s, config.robot().boards());
      if (!made.ok()) throw std::runtime_error(made.status().ToString());
      auto source = std::shared_ptr<robot::perception::PerceptionInterface>(std::move(*made));
      const auto initialized = source->Init();
      if (!initialized.ok()) throw std::runtime_error(initialized.ToString());
      sources_.push_back(source);
      if (s.sensor_type() == robot::perception::POSITION) {
        for (const auto& p : s.node().publishers()) {
          auto publish = ros2_utils::CreatePositionMessagePublisher(*this, p, s);
          if (!publish.ok()) throw std::runtime_error(publish.status().ToString());
          timers_.push_back(
              create_wall_timer(std::chrono::nanoseconds(1000000000ull / p.publish_rate_hz()),
                                [source, publish = *publish] {
                                  auto packet = source->GetData();
                                  if (!packet.ok()) return;
                                  auto value = ros2_utils::RequirePerceptionPosition(*packet);
                                  if (value.ok()) publish(*value).IgnoreError();
                                }));
        }
        continue;
      }
      for (const auto& p : s.node().publishers()) {
        auto publisher = create_publisher<std_msgs::msg::UInt8MultiArray>(
            p.topic(), ros2_utils::CreateQosSetting(s.node().qos_setting()));
        auto health = create_publisher<std_msgs::msg::String>(p.topic() + "/health", 1);
        timers_.push_back(create_wall_timer(
            std::chrono::nanoseconds(1000000000ull / p.publish_rate_hz()),
            [source, publisher, health] {
              auto packet = source->GetData();
              std_msgs::msg::String state;
              state.data = packet.ok() ? (packet->scalar().usable() ? "usable" : "invalid_or_stale")
                                       : packet.status().ToString();
              health->publish(state);
              if (!packet.ok()) return;
              const auto bytes = packet->SerializeAsString();
              std_msgs::msg::UInt8MultiArray message;
              message.data.assign(bytes.begin(), bytes.end());
              publisher->publish(message);
            }));
      }
    }
    for (const auto& b : config.robot().boards()) {
      if (!names.count(b.name())) continue;
      auto board = robot::board::BoardFactory::GetOrCreate(b);
      if (!board.ok()) throw std::runtime_error(board.status().ToString());
      boards_.push_back(*board);
    }
    stop_group_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    stop_ = create_service<std_srvs::srv::Trigger>(
        "~/estop",
        [this](std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> reply) {
          const auto disabled = StopActions();
          reply->success = disabled.ok();
          if (!disabled.ok()) reply->message = disabled.ToString();
          for (auto& b : boards_) {
            const auto status = b->EmergencyStop();
            if (!status.ok() && !absl::IsUnimplemented(status)) {
              reply->success = false;
              reply->message += status.ToString() + "; ";
            }
          }
          if (reply->success)
            reply->message =
                "Host stop interlocked; available board stop acknowledged; physical verification "
                "unsupported";
        },
        rmw_qos_profile_services_default,
        stop_group_);
  }
  ~DeviceBus() override {
    timers_.clear();
    ShutdownActions().IgnoreError();
    for (auto& s : sources_) s->Teardown().IgnoreError();
    for (auto& b : boards_) b->Teardown().IgnoreError();
  }

 private:
  std::vector<std::shared_ptr<robot::perception::PerceptionInterface>> sources_;
  std::vector<std::shared_ptr<robot::board::BoardInterface>> boards_;
  std::vector<rclcpp::TimerBase::SharedPtr> timers_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr stop_;
  rclcpp::CallbackGroup::SharedPtr stop_group_;
};
