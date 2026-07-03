#pragma once

#include <memory>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/robot.pb.h"
#include "robot/action/interfaces/action_interface.h"
#include "robot/action/motors/drivers/am243_ethercat_driver.h"
#include "robot/action/motors/drivers/sts3215_driver.h"
#include "robot/comm/factory/comm_factory.h"
#include "utils/status_macros.h"

namespace robot::action {
class ActionFactory {
 public:
  static absl::StatusOr<std::unique_ptr<robot::action::ActionInterface>> CreateAction(
      const robot::action::SingleAction& single_action) {
    switch (single_action.action_type()) {
      case robot::action::ActionType::ACTUATOR: {
        const auto& actuator = single_action.actuator();
        switch (actuator.actuator_type()) {
          case robot::action::ActuatorType::STS3215_SERVO: {
            ABSL_ASSIGN_OR_RETURN(auto serial,
                                  robot::comm::CommFactory::CreateSerial(actuator.comm()));
            auto driver = std::make_unique<robot::action::Sts3215Driver>(serial, actuator);
            ABSL_RETURN_IF_ERROR(driver->Init());
            return driver;
          }
          case robot::action::ActuatorType::AM243_ETHERCAT_ACTUATOR: {
            if (actuator.comm().comm_type() != robot::comm::CommType::ETHERCAT ||
                !actuator.comm().has_ethercat_config()) {
              return absl::Status(absl::StatusCode::kInvalidArgument,
                                  "AM243 EtherCAT actuator requires EtherCAT comm config.");
            }
            if (actuator.comm().ethercat_config().process_data_mode() !=
                robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR) {
              return absl::Status(absl::StatusCode::kInvalidArgument,
                                  "AM243 EtherCAT actuator requires split LRD/LWR process data.");
            }
            ABSL_ASSIGN_OR_RETURN(
                auto ethercat, robot::comm::CommFactory::CreateEthercatTransport(actuator.comm()));
            auto driver = std::make_unique<robot::action::Am243EthercatDriver>(ethercat, actuator);
            ABSL_RETURN_IF_ERROR(driver->Init());
            return driver;
          }
          default:
            return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid actuator type.");
        }
      }
      // TODO: Add other action types here when they are implemented
      // case robot::action::ActionType::GRIPPER:
      // case robot::action::ActionType::END_EFFECTOR:
      default:
        return absl::Status(absl::StatusCode::kInvalidArgument, "Invalid action type.");
    }
  }

  ~ActionFactory() = default;
  ActionFactory(const ActionFactory&) = delete;
  ActionFactory& operator=(const ActionFactory&) = delete;
  ActionFactory(ActionFactory&&) = default;
  ActionFactory& operator=(ActionFactory&&) = default;

 private:
  ActionFactory() = default;
};
}  // namespace robot::action
