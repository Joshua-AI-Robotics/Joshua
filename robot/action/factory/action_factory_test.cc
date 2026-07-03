#include "robot/action/factory/action_factory.h"

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"
#include "gtest/gtest.h"

namespace robot::action {
namespace {

robot::action::SingleAction MakeAm243EthercatSingleAction() {
  robot::action::SingleAction single_action;
  single_action.set_action_type(robot::action::ActionType::ACTUATOR);

  auto* actuator = single_action.mutable_actuator();
  actuator->set_actuator_name("am243_joint_1");
  actuator->set_id(1);
  actuator->set_actuator_type(robot::action::ActuatorType::AM243_ETHERCAT_ACTUATOR);
  actuator->set_physical_lower_limit(-180.0f);
  actuator->set_physical_upper_limit(180.0f);
  actuator->set_operational_lower_limit(-90.0f);
  actuator->set_operational_upper_limit(90.0f);

  auto* comm = actuator->mutable_comm();
  comm->set_comm_type(robot::comm::CommType::ETHERCAT);
  auto* ethercat_config = comm->mutable_ethercat_config();
  ethercat_config->set_interface_name("joshua-no-such-ethercat-iface0");
  ethercat_config->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR);

  auto* actuator_config = actuator->mutable_am243_ethercat_config();
  actuator_config->set_slave_index(2);
  actuator_config->set_output_size_bytes(8);
  actuator_config->set_input_size_bytes(8);
  actuator_config->set_idle_position(0.0f);
  actuator_config->set_pdo_mapping(robot::action::Am243PdoMapping::AM243_PDO_MAPPING_TI_DEMO);

  return single_action;
}

TEST(ActionFactoryTest, Am243EthercatActionReportsUnavailableForMissingInterface) {
  auto action_or = robot::action::ActionFactory::CreateAction(MakeAm243EthercatSingleAction());

  EXPECT_EQ(action_or.status().code(), absl::StatusCode::kUnavailable);
}

TEST(ActionFactoryTest, Am243EthercatActionRejectsInvalidEthercatCommConfig) {
  auto single_action = MakeAm243EthercatSingleAction();
  single_action.mutable_actuator()
      ->mutable_comm()
      ->mutable_ethercat_config()
      ->clear_interface_name();

  auto action_or = robot::action::ActionFactory::CreateAction(single_action);

  EXPECT_EQ(action_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(ActionFactoryTest, Am243EthercatActionRejectsLrwProcessDataMode) {
  auto single_action = MakeAm243EthercatSingleAction();
  single_action.mutable_actuator()
      ->mutable_comm()
      ->mutable_ethercat_config()
      ->set_process_data_mode(robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_LRW);

  auto action_or = robot::action::ActionFactory::CreateAction(single_action);

  EXPECT_EQ(action_or.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace robot::action
