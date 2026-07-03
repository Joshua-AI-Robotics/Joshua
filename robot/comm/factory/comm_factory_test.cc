#include "robot/comm/factory/comm_factory.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::comm {
namespace {

robot::comm::Comm MakeEthercatComm() {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::ETHERCAT);
  auto* config = comm.mutable_ethercat_config();
  config->set_interface_name("enp5s0");
  config->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR);
  return comm;
}

TEST(CommFactoryTest, CreateEthercatTransportRejectsWrongCommType) {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::SERIAL);

  auto transport_or = CommFactory::CreateEthercatTransport(comm);

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, CreateEthercatTransportRejectsMissingConfig) {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::ETHERCAT);

  auto transport_or = CommFactory::CreateEthercatTransport(comm);

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, CreateEthercatTransportRejectsMissingInterfaceName) {
  auto comm = MakeEthercatComm();
  comm.mutable_ethercat_config()->clear_interface_name();

  auto transport_or = CommFactory::CreateEthercatTransport(comm);

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, CreateEthercatTransportRejectsInvalidProcessDataMode) {
  auto comm = MakeEthercatComm();
  comm.mutable_ethercat_config()->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_INVALID);

  auto transport_or = CommFactory::CreateEthercatTransport(comm);

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, CreateEthercatTransportReturnsUnimplementedUntilBackendExists) {
  auto transport_or = CommFactory::CreateEthercatTransport(MakeEthercatComm());

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kUnimplemented);
}

}  // namespace
}  // namespace robot::comm
