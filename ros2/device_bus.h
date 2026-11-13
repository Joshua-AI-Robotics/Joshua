#pragma once
// Mixed MCU sources and commands deliberately share this process and factory.
#include <future>
#include <map>

#include "absl/status/status.h"
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
          const auto requests = RequestStopActions();
          reply->success = true;
          // Each board performs its own fallback before waiting on other boards.
          // Keep the bus-wide requests pending until all stop work has finished.
          std::map<std::string, std::future<absl::Status>> stops;
          for (const auto& [name, board] : boards_)
            stops.emplace(name, std::async(std::launch::async, [this, name, board] {
                            auto status = board->EmergencyStop();
                            const auto disabled = StopBoardActions(name, status.ok());
                            if (absl::IsUnimplemented(status)) status = absl::OkStatus();
                            status.Update(disabled);
                            return status;
                          }));
          for (auto& [name, stop] : stops) {
            const auto status = stop.get();
            if (!status.ok()) {
              reply->success = false;
              reply->message += status.ToString() + "; ";
            }
          }
          const auto completed = CompleteStopActions(requests);
          if (!completed.ok()) {
            reply->success = false;
            reply->message += completed.ToString() + "; ";
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
    ShutdownActions();
    observations_.clear();
    for (auto& [name, b] : boards_)
      joshua::RunCleanup("device board teardown", [&] { joshua::ThrowIfError(b->Teardown()); });
  }

 private:
  friend class McuBusTest;
  friend class DeviceBusTest;
  std::vector<std::unique_ptr<ros2_utils::ObservationPublisher>> observations_;
  std::map<std::string, std::shared_ptr<robot::board::BoardInterface>> boards_;
  rclcpp::Service<std_srvs::srv::Trigger>::SharedPtr stop_;
  rclcpp::CallbackGroup::SharedPtr stop_group_;
};
