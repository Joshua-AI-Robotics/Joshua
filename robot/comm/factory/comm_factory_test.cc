#include "robot/comm/factory/comm_factory.h"

#include <memory>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "robot/comm/ethercat/fake_ethercat_transport.h"
#include "robot/comm/interfaces/comm_lease.h"
#include "robot/comm/interfaces/message_framer.h"
#include "robot/comm/proto/comm.pb.h"
#include "robot/comm/testing/fake_correlated_cyclic_transport.h"
#include "robot/comm/testing/fake_message_transport.h"

namespace robot::comm {
namespace {

using robot::comm::ethercat::FakeEthercatTransport;

class OneByteFramer : public MessageFramer {
 public:
  absl::StatusOr<size_t> RemainingBytes(absl::Span<const uint8_t> received) const override {
    return received.empty() ? 1 : 0;
  }
};

CommOptions WithFramer() {
  CommOptions options;
  options.message_framer = std::make_shared<OneByteFramer>();
  return options;
}

robot::comm::Comm MakeSerialComm(TransportType transport) {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::SERIAL);
  comm.set_transport_type(transport);
  comm.mutable_serial_config()->set_port("/dev/joshua-no-such-port");
  comm.mutable_serial_config()->set_baudrate(115200);
  return comm;
}

robot::comm::Comm MakeEthercatComm() {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::ETHERCAT);
  comm.set_transport_type(robot::comm::TransportType::CYCLIC);
  auto* config = comm.mutable_ethercat_config();
  config->set_interface_name("joshua-no-such-ethercat-iface0");
  config->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR);
  return comm;
}

TEST(CommFactoryTest, AcquireRejectsMissingTransportType) {
  auto comm = MakeSerialComm(TransportType::TRANSPORT_INVALID);

  EXPECT_EQ(CommFactory::Acquire(comm).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, AcquireRejectsUnsupportedMechanismCapabilityPair) {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::ETHERNET_UDP);
  comm.set_transport_type(robot::comm::TransportType::BYTE_STREAM);

  EXPECT_EQ(CommFactory::Acquire(comm).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, AcquireRejectsMissingEthercatConfig) {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::ETHERCAT);
  comm.set_transport_type(robot::comm::TransportType::CYCLIC);

  EXPECT_EQ(CommFactory::Acquire(comm).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, AcquireRejectsSerialMessageWithoutFramerBeforeOpening) {
  auto comm = MakeSerialComm(TransportType::MESSAGE);

  EXPECT_EQ(CommFactory::Acquire(comm).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, AcquireRejectsCyclicCapabilitiesOnSerial) {
  EXPECT_EQ(CommFactory::Acquire(MakeSerialComm(TransportType::CYCLIC)).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(CommFactory::Acquire(MakeSerialComm(TransportType::CORRELATED_CYCLIC)).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, AcquireReportsEthercatMailboxAndCorrelatedPdoAsUnimplemented) {
  auto comm = MakeEthercatComm();
  comm.clear_transport_type();
  comm.add_required_transports(TransportType::MESSAGE);
  comm.add_required_transports(TransportType::CORRELATED_CYCLIC);

  EXPECT_EQ(CommFactory::Acquire(comm).status().code(), absl::StatusCode::kUnimplemented);
}

TEST(CommFactoryTest, CreateEthercatTransportRejectsMissingInterfaceName) {
  auto comm = MakeEthercatComm();
  comm.mutable_ethercat_config()->clear_interface_name();

  auto transport_or = CommFactory::CreateEthercat(comm.ethercat_config());

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, CreateEthercatTransportRejectsInvalidProcessDataMode) {
  auto comm = MakeEthercatComm();
  comm.mutable_ethercat_config()->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_INVALID);

  auto transport_or = CommFactory::CreateEthercat(comm.ethercat_config());

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, CreateEthercatTransportReportsUnavailableForMissingInterface) {
  auto comm = MakeEthercatComm();
  auto transport_or = CommFactory::CreateEthercat(comm.ethercat_config());

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kUnavailable);
}

class CommFactoryLeaseTest : public ::testing::Test {
 protected:
  void TearDown() override {
    CommFactory::SetCommLeaseFactoryForTesting(nullptr);
  }
};

TEST_F(CommFactoryLeaseTest, OneLeaseCanExposeSeveralCapabilitiesOfOneLink) {
  auto message = std::make_shared<FakeMessageTransport>();
  auto cyclic = std::make_shared<FakeCorrelatedCyclicTransport>();
  CommFactory::SetCommLeaseFactoryForTesting(
      [&](const Comm&, const CommOptions&) -> absl::StatusOr<CommLease> {
        CommCapabilities capabilities;
        capabilities.message = message;
        capabilities.correlated_cyclic = cyclic;
        return CommLease(std::move(capabilities));
      });
  auto comm = MakeSerialComm(TransportType::MESSAGE);

  auto lease = CommFactory::Acquire(comm, WithFramer());

  ASSERT_TRUE(lease.ok()) << lease.status();
  auto selected_message = lease->Require<MessageTransport>();
  auto selected_cyclic = lease->Require<CorrelatedCyclicTransport>();
  ASSERT_TRUE(selected_message.ok()) << selected_message.status();
  ASSERT_TRUE(selected_cyclic.ok()) << selected_cyclic.status();
  EXPECT_EQ(selected_message->get(), message.get());
  EXPECT_EQ(selected_cyclic->get(), cyclic.get());
  EXPECT_EQ(lease->Require<ByteStream>().status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(CommFactoryLeaseTest, PassesConsumerFramerToTheLinkFactory) {
  auto options = WithFramer();
  const MessageFramer* seen_framer = nullptr;
  CommFactory::SetCommLeaseFactoryForTesting(
      [&](const Comm&, const CommOptions& passed) -> absl::StatusOr<CommLease> {
        seen_framer = passed.message_framer.get();
        CommCapabilities capabilities;
        capabilities.message = std::make_shared<FakeMessageTransport>();
        return CommLease(std::move(capabilities));
      });

  ASSERT_TRUE(CommFactory::Acquire(MakeSerialComm(TransportType::MESSAGE), options).ok());

  EXPECT_EQ(seen_framer, options.message_framer.get());
}

TEST_F(CommFactoryLeaseTest, RejectsLeaseMissingARequiredCapability) {
  CommFactory::SetCommLeaseFactoryForTesting(
      [](const Comm&, const CommOptions&) -> absl::StatusOr<CommLease> { return CommLease(); });

  auto lease = CommFactory::Acquire(MakeSerialComm(TransportType::MESSAGE), WithFramer());

  EXPECT_EQ(lease.status().code(), absl::StatusCode::kInternal);
}

TEST_F(CommFactoryLeaseTest, ValidatesConfigBeforeCallingTheLinkFactory) {
  bool called = false;
  CommFactory::SetCommLeaseFactoryForTesting(
      [&called](const Comm&, const CommOptions&) -> absl::StatusOr<CommLease> {
        called = true;
        return CommLease();
      });

  auto lease = CommFactory::Acquire(MakeSerialComm(TransportType::CORRELATED_CYCLIC));

  EXPECT_EQ(lease.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_FALSE(called);
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

TEST_F(CommFactoryEthercatCacheTest, AcquireExposesProcessImageOnly) {
  auto lease = CommFactory::Acquire(MakeEthercatComm());

  ASSERT_TRUE(lease.ok()) << lease.status();
  EXPECT_TRUE(lease->Require<ethercat::EthercatTransport>().ok());
  EXPECT_EQ(lease->Require<MessageTransport>().status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(CommFactoryEthercatCacheTest, LeasesOnOneInterfaceShareOneMaster) {
  auto first = CommFactory::Acquire(MakeEthercatComm());
  auto second = CommFactory::Acquire(MakeEthercatComm());

  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(first->capabilities().process_image.get(), second->capabilities().process_image.get());
}

TEST_F(CommFactoryEthercatCacheTest, SameInterfaceSharesOneMaster) {
  auto comm = MakeEthercatComm();
  auto first_or = CommFactory::CreateEthercat(comm.ethercat_config());
  auto second_or = CommFactory::CreateEthercat(comm.ethercat_config());

  ASSERT_TRUE(first_or.ok()) << first_or.status();
  ASSERT_TRUE(second_or.ok()) << second_or.status();
  EXPECT_EQ(first_or->get(), second_or->get());
}

TEST_F(CommFactoryEthercatCacheTest, DifferentInterfacesGetDifferentMasters) {
  auto first_comm = MakeEthercatComm();
  auto first_or = CommFactory::CreateEthercat(first_comm.ethercat_config());
  auto other_comm = MakeEthercatComm();
  other_comm.mutable_ethercat_config()->set_interface_name("joshua-no-such-ethercat-iface1");
  auto second_or = CommFactory::CreateEthercat(other_comm.ethercat_config());

  ASSERT_TRUE(first_or.ok()) << first_or.status();
  ASSERT_TRUE(second_or.ok()) << second_or.status();
  EXPECT_NE(first_or->get(), second_or->get());
}

TEST_F(CommFactoryEthercatCacheTest, RejectsProcessDataModeChangeOnOpenInterface) {
  auto first_comm = MakeEthercatComm();
  auto first_or = CommFactory::CreateEthercat(first_comm.ethercat_config());
  ASSERT_TRUE(first_or.ok()) << first_or.status();

  auto lrw_comm = MakeEthercatComm();
  lrw_comm.mutable_ethercat_config()->set_process_data_mode(
      robot::comm::EthercatProcessDataMode::ETHERCAT_PROCESS_DATA_MODE_LRW);
  auto second_or = CommFactory::CreateEthercat(lrw_comm.ethercat_config());

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

  auto comm = MakeEthercatComm();
  auto failed_or = CommFactory::CreateEthercat(comm.ethercat_config());
  EXPECT_EQ(failed_or.status().code(), absl::StatusCode::kUnavailable);

  auto retry_or = CommFactory::CreateEthercat(comm.ethercat_config());
  EXPECT_TRUE(retry_or.ok()) << retry_or.status();
  EXPECT_EQ(factory_calls, 2);
}

}  // namespace
}  // namespace robot::comm
