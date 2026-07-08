#include "robot/comm/factory/comm_factory.h"

#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/ethercat/fake_ethercat_transport.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::comm {
namespace {

using robot::comm::ethercat::FakeEthercatTransport;

robot::comm::Comm MakeEthercatComm() {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::ETHERCAT);
  auto* config = comm.mutable_ethercat_config();
  config->set_interface_name("joshua-no-such-ethercat-iface0");
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

TEST(CommFactoryTest, CreateEthercatTransportReportsUnavailableForMissingInterface) {
  auto transport_or = CommFactory::CreateEthercatTransport(MakeEthercatComm());

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kUnavailable);
}

class CommFactoryEthercatCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    CommFactory::SetEthercatTransportFactoryForTesting(
        [] { return std::make_shared<FakeEthercatTransport>(); });
  }

  void TearDown() override {
    CommFactory::SetEthercatTransportFactoryForTesting(nullptr);
    CommFactory::ResetEthercatTransportCacheForTesting();
  }
};

TEST_F(CommFactoryEthercatCacheTest, SameInterfaceSharesOneMaster) {
  auto first_or = CommFactory::CreateEthercatTransport(MakeEthercatComm());
  auto second_or = CommFactory::CreateEthercatTransport(MakeEthercatComm());

  ASSERT_TRUE(first_or.ok()) << first_or.status();
  ASSERT_TRUE(second_or.ok()) << second_or.status();
  EXPECT_EQ(first_or->get(), second_or->get());
}

TEST_F(CommFactoryEthercatCacheTest, DifferentInterfacesGetDifferentMasters) {
  auto first_or = CommFactory::CreateEthercatTransport(MakeEthercatComm());
  auto other_comm = MakeEthercatComm();
  other_comm.mutable_ethercat_config()->set_interface_name("joshua-no-such-ethercat-iface1");
  auto second_or = CommFactory::CreateEthercatTransport(other_comm);

  ASSERT_TRUE(first_or.ok()) << first_or.status();
  ASSERT_TRUE(second_or.ok()) << second_or.status();
  EXPECT_NE(first_or->get(), second_or->get());
}

TEST_F(CommFactoryEthercatCacheTest, RejectsProcessDataModeChangeOnOpenInterface) {
  auto first_or = CommFactory::CreateEthercatTransport(MakeEthercatComm());
  ASSERT_TRUE(first_or.ok()) << first_or.status();

  auto lrw_comm = MakeEthercatComm();
  lrw_comm.mutable_ethercat_config()->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_LRW);
  auto second_or = CommFactory::CreateEthercatTransport(lrw_comm);

  EXPECT_EQ(second_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(CommFactoryEthercatCacheTest, FailedInitIsNotCached) {
  int factory_calls = 0;
  CommFactory::SetEthercatTransportFactoryForTesting([&factory_calls] {
    factory_calls++;
    auto transport = std::make_shared<FakeEthercatTransport>();
    if (factory_calls == 1) {
      transport->init_status_ = absl::Status(absl::StatusCode::kUnavailable, "no NIC");
    }
    return transport;
  });

  auto failed_or = CommFactory::CreateEthercatTransport(MakeEthercatComm());
  EXPECT_EQ(failed_or.status().code(), absl::StatusCode::kUnavailable);

  auto retry_or = CommFactory::CreateEthercatTransport(MakeEthercatComm());
  EXPECT_TRUE(retry_or.ok()) << retry_or.status();
  EXPECT_EQ(factory_calls, 2);
}

}  // namespace
}  // namespace robot::comm
