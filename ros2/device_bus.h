#pragma once
// Mixed MCU sources and commands deliberately share this process and factory.
#include <map>

#include "robot/board/factory/board_factory.h"
#include "robot/perception/factory/perception_factory.h"
#include "ros2/action_subscriber.h"
#include "ros2/observation_publisher.h"
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
      observations_.push_back(
          std::make_unique<ros2_utils::ObservationPublisher>(*this, s, config.robot().boards()));
    }
    for (const auto& b : config.robot().boards()) {
      if (!names.count(b.name())) continue;
      auto board = robot::board::BoardFactory::GetOrCreate(b);
      if (!board.ok()) throw std::runtime_error(board.status().ToString());
      boards_.emplace(b.name(), *board);
    }
    stop_group_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    stop_ = create_service<std_srvs::srv::Trigger>(
        "~/estop",
        [this](std::shared_ptr<std_srvs::srv::Trigger::Request>,
               std::shared_ptr<std_srvs::srv::Trigger::Response> reply) {
          RequestStopActions();
          reply->success = true;
          std::set<std::string> stopped_boards;
          for (auto& [name, b] : boards_) {
            const auto status = b->EmergencyStop();
            if (status.ok()) stopped_boards.insert(name);
            if (!status.ok() && !absl::IsUnimplemented(status)) {
              reply->success = false;
              reply->message += status.ToString() + "; ";
            }
          }
          const auto disabled = StopActions(stopped_boards);
          if (!disabled.ok()) {
            reply->success = false;
            reply->message += disabled.ToString() + "; ";
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
    ShutdownActions().IgnoreError();
    observations_.clear();
    for (auto& [name, b] : boards_) b->Teardown().IgnoreError();
  }

 private:
  std::vector<std::unique_ptr<ros2_utils::ObservationPublisher>> observations_;
  std::map<std::string, std::shared_ptr<robot::board::BoardInterface>> boards_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr stop_;
  rclcpp::CallbackGroup::SharedPtr stop_group_;
};
