#include "robot/action/motors/drivers/am243_ethercat_driver.h"

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/action/motors/drivers/am243_pdo_codec.h"
#include "robot/action/proto/action_packet.pb.h"
#include "robot/comm/ethercat/fake_ethercat_transport.h"

namespace robot::action {
namespace {

using robot::comm::ethercat::FakeEthercatTransport;

robot::action::Actuator MakeAm243Actuator() {
  robot::action::Actuator actuator;
  actuator.set_actuator_name("joint_1");
  actuator.set_id(7);
  actuator.set_actuator_type(robot::action::ActuatorType::AM243_ETHERCAT_ACTUATOR);
  actuator.set_physical_lower_limit(-180.0f);
  actuator.set_physical_upper_limit(180.0f);
  actuator.set_operational_lower_limit(-90.0f);
  actuator.set_operational_upper_limit(90.0f);

  auto* config = actuator.mutable_am243_ethercat_config();
  config->set_slave_index(2);
  config->set_output_offset_bytes(4);
  config->set_input_offset_bytes(12);
  config->set_output_size_bytes(8);
  config->set_input_size_bytes(8);
  config->set_idle_position(0.0f);
  return actuator;
}

TEST(Am243EthercatDriverTest, InitRejectsNullTransport) {
  Am243EthercatDriver driver(nullptr, MakeAm243Actuator());

  auto status = driver.Init();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(Am243EthercatDriverTest, InitRejectsMissingAm243Config) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  robot::action::Actuator actuator;
  actuator.set_actuator_name("joint_1");
  actuator.set_id(7);
  actuator.set_actuator_type(robot::action::ActuatorType::AM243_ETHERCAT_ACTUATOR);

  Am243EthercatDriver driver(transport, actuator);

  auto status = driver.Init();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(Am243EthercatDriverTest, InitUsesExplicitPdoRegionConfig) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  Am243EthercatDriver driver(transport, MakeAm243Actuator());

  auto status = driver.Init();

  EXPECT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport->get_pdo_region_calls_, 0);
}

TEST(Am243EthercatDriverTest, InitRejectsInvalidExplicitPdoRegionConfig) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  auto actuator = MakeAm243Actuator();
  actuator.mutable_am243_ethercat_config()->set_output_size_bytes(8);
  actuator.mutable_am243_ethercat_config()->set_input_size_bytes(0);
  Am243EthercatDriver driver(transport, actuator);

  auto status = driver.Init();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(Am243EthercatDriverTest, InitFetchesPdoRegionWhenSizesAreZero) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  auto actuator = MakeAm243Actuator();
  actuator.mutable_am243_ethercat_config()->set_output_size_bytes(0);
  actuator.mutable_am243_ethercat_config()->set_input_size_bytes(0);
  Am243EthercatDriver driver(transport, actuator);

  auto status = driver.Init();

  EXPECT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport->get_pdo_region_calls_, 1);
  EXPECT_EQ(transport->requested_slave_index_, 2);
}

TEST(Am243EthercatDriverTest, InitReturnsTransportPdoRegionError) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  transport->get_pdo_region_status_ =
      absl::Status(absl::StatusCode::kUnavailable, "PDO region unavailable");
  auto actuator = MakeAm243Actuator();
  actuator.mutable_am243_ethercat_config()->set_output_size_bytes(0);
  actuator.mutable_am243_ethercat_config()->set_input_size_bytes(0);
  Am243EthercatDriver driver(transport, actuator);

  auto status = driver.Init();

  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
}

TEST(Am243EthercatDriverTest, InitRejectsInvalidFetchedPdoRegion) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  transport->pdo_region_.output_size_bytes = 8;
  transport->pdo_region_.input_size_bytes = 0;
  auto actuator = MakeAm243Actuator();
  actuator.mutable_am243_ethercat_config()->set_output_size_bytes(0);
  actuator.mutable_am243_ethercat_config()->set_input_size_bytes(0);
  Am243EthercatDriver driver(transport, actuator);

  auto status = driver.Init();

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(Am243EthercatDriverTest, SetCommandsValidateInputsAndSendDemoPdos) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  Am243EthercatDriver driver(transport, MakeAm243Actuator());
  ASSERT_TRUE(driver.Init().ok());

  EXPECT_EQ(driver.SetSpeed(-1.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(driver.SetTorque(-1.0f).code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(driver.SetPosition(100.0f).code(), absl::StatusCode::kInvalidArgument);

  EXPECT_TRUE(driver.SetSpeed(1.0f).ok());
  EXPECT_TRUE(driver.SetTorque(1.0f).ok());
  EXPECT_TRUE(driver.SetPosition(0.0f).ok());
}

TEST(Am243EthercatDriverTest, SetPositionWritesDemoPdoAndExchangesProcessData) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  transport->process_data_.working_count = 3;
  transport->process_data_.expected_working_count = 3;
  Am243EthercatDriver driver(transport, MakeAm243Actuator());
  ASSERT_TRUE(driver.Init().ok());

  auto status = driver.SetPosition(0.0f);

  EXPECT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport->exchange_process_data_calls_, 1);
  EXPECT_EQ(transport->last_write_region_.slave_index, 2);
  EXPECT_EQ(transport->last_outputs_, robot::action::am243::EncodeDemoOutputWalk(128));
}

TEST(Am243EthercatDriverTest, SetSpeedWritesRoundedDemoPdoSeed) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  Am243EthercatDriver driver(transport, MakeAm243Actuator());
  ASSERT_TRUE(driver.Init().ok());

  auto status = driver.SetSpeed(3.2f);

  EXPECT_TRUE(status.ok()) << status;
  EXPECT_EQ(transport->last_outputs_, robot::action::am243::EncodeDemoOutputWalk(3));
}

TEST(Am243EthercatDriverTest, CommandReturnsWorkingCountMismatch) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  transport->process_data_.working_count = 2;
  transport->process_data_.expected_working_count = 3;
  Am243EthercatDriver driver(transport, MakeAm243Actuator());
  ASSERT_TRUE(driver.Init().ok());

  auto status = driver.SetSpeed(1.0f);

  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
}

TEST(Am243EthercatDriverTest, SetActionRoutesPresetTeardownToHarmlessTeardown) {
  auto transport = std::make_shared<FakeEthercatTransport>();
  Am243EthercatDriver driver(transport, MakeAm243Actuator());
  ASSERT_TRUE(driver.Init().ok());

  robot::action::ActionPacket packet;
  packet.set_preset(robot::action::PresetCommand::PRESET_TEARDOWN);

  EXPECT_TRUE(driver.SetAction(packet).ok());
}

}  // namespace
}  // namespace robot::action
