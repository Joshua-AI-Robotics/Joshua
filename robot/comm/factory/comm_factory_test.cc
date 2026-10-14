#include "robot/comm/factory/comm_factory.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/ethercat/fake_ethercat_transport.h"
#include "robot/comm/proto/comm.pb.h"
#include "robot/comm/testing/fake_transports.h"

namespace robot::comm {
namespace {

using robot::comm::ethercat::FakeEthercatTransport;

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

TEST(CommFactoryTest, CreateCommRejectsMissingTransportType) {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::SERIAL);
  comm.mutable_serial_config()->set_port("/dev/ttyUSB0");
  comm.mutable_serial_config()->set_baudrate(115200);

  auto transport = CommFactory::CreateComm(comm);

  EXPECT_EQ(transport.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, CreateCommRejectsUnsupportedMechanismCapabilityPair) {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::ETHERNET_UDP);
  comm.set_transport_type(robot::comm::TransportType::BYTE_STREAM);

  auto transport = CommFactory::CreateComm(comm);

  EXPECT_EQ(transport.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, CreateCommRejectsMissingEthercatConfig) {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::ETHERCAT);
  comm.set_transport_type(robot::comm::TransportType::CYCLIC);

  auto transport_or = CommFactory::CreateComm(comm);

  EXPECT_EQ(transport_or.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, CapabilitySelectionRejectsMissingOrNullEndpoints) {
  auto message = std::make_shared<testing::FakeMessageTransport>();
  CommTransport selected{std::static_pointer_cast<MessageTransport>(message)};
  ASSERT_TRUE(GetCommTransport<MessageTransport>(selected).ok());
  EXPECT_EQ(GetCommTransport<CorrelatedCyclicTransport>(selected).status().code(),
            absl::StatusCode::kInvalidArgument);
  auto cyclic = std::make_shared<testing::FakeCorrelatedCyclicTransport>();
  selected = std::static_pointer_cast<CorrelatedCyclicTransport>(cyclic);
  ASSERT_TRUE(GetCommTransport<CorrelatedCyclicTransport>(selected).ok());
  EXPECT_EQ(GetCommTransport<MessageTransport>(selected).status().code(),
            absl::StatusCode::kInvalidArgument);
  selected = std::shared_ptr<MessageTransport>{};
  EXPECT_EQ(GetCommTransport<MessageTransport>(selected).status().code(),
            absl::StatusCode::kInvalidArgument);
  selected = std::shared_ptr<CorrelatedCyclicTransport>{};
  EXPECT_EQ(GetCommTransport<CorrelatedCyclicTransport>(selected).status().code(),
            absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, UnsupportedCapabilitiesAreRejectedBeforeOpeningDevices) {
  robot::comm::Comm serial;
  serial.set_comm_type(SERIAL);
  serial.set_transport_type(CYCLIC);
  serial.mutable_serial_config()->set_port("never-open-this-port");
  serial.mutable_serial_config()->set_baudrate(115200);
  EXPECT_EQ(CommFactory::CreateComm(serial).status().code(), absl::StatusCode::kInvalidArgument);
  serial.set_transport_type(static_cast<TransportType>(999));
  EXPECT_EQ(CommFactory::CreateComm(serial).status().code(), absl::StatusCode::kInvalidArgument);
  auto ethercat = MakeEthercatComm();
  ethercat.set_transport_type(MESSAGE);
  EXPECT_EQ(CommFactory::CreateComm(ethercat).status().code(), absl::StatusCode::kInvalidArgument);
}

class CommFactorySerialCacheTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // A fresh pseudo-terminal exercises the real serial factory without hardware.
    master_fd_ = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_GE(master_fd_, 0);
    ASSERT_EQ(grantpt(master_fd_), 0);
    ASSERT_EQ(unlockpt(master_fd_), 0);
    const char* port = ptsname(master_fd_);
    ASSERT_NE(port, nullptr);
    comm_.set_comm_type(SERIAL);
    comm_.set_transport_type(BYTE_STREAM);
    comm_.mutable_serial_config()->set_port(port);
    comm_.mutable_serial_config()->set_baudrate(115200);
  }

  void TearDown() override {
    CommFactory::ResetSerialTransportCacheForTesting();
    if (master_fd_ >= 0) close(master_fd_);
  }

  int master_fd_ = -1;
  robot::comm::Comm comm_;
};

TEST_F(CommFactorySerialCacheTest, SamePortAndBaudRateShareOneByteStream) {
  auto first = CommFactory::CreateComm(comm_);
  auto second = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  auto first_stream = GetCommTransport<ByteStream>(*first);
  auto second_stream = GetCommTransport<ByteStream>(*second);
  ASSERT_TRUE(first_stream.ok());
  ASSERT_TRUE(second_stream.ok());
  EXPECT_EQ(*first_stream, *second_stream);
}

TEST_F(CommFactorySerialCacheTest, MessageAdaptersShareOneSerialBusLock) {
  comm_.set_transport_type(MESSAGE);
  comm_.mutable_serial_config()->set_exchange_timeout_ms(500);
  auto slow = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(slow.ok()) << slow.status();
  auto slow_message = GetCommTransport<MessageTransport>(*slow);
  ASSERT_TRUE(slow_message.ok());
  comm_.mutable_serial_config()->set_exchange_timeout_ms(25);
  auto fast = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(fast.ok()) << fast.status();
  auto fast_message = GetCommTransport<MessageTransport>(*fast);
  ASSERT_TRUE(fast_message.ok());

  const std::vector<uint8_t> request{
      0xa5, 0x0b, 0x02, 0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01, 0x08, 0xff, 0x82, 0x0c};
  auto pending = std::async(std::launch::async, [&] { return (*slow_message)->Exchange(request); });
  std::vector<uint8_t> observed(request.size());
  size_t received = 0;
  pollfd descriptor{master_fd_, POLLIN, 0};
  while (received < observed.size()) {
    ASSERT_GT(poll(&descriptor, 1, 1000), 0);
    const ssize_t count = read(master_fd_, observed.data() + received, observed.size() - received);
    ASSERT_GT(count, 0);
    received += static_cast<size_t>(count);
  }
  ASSERT_EQ(observed, request);  // The first exchange now owns the serial bus.
  auto rejected = (*fast_message)->Exchange(request);
  EXPECT_EQ(rejected.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_NE(rejected.status().message().find("not sent"), std::string::npos);
  EXPECT_EQ(poll(&descriptor, 1, 10), 0);

  ASSERT_EQ(write(master_fd_, request.data(), request.size()),
            static_cast<ssize_t>(request.size()));
  auto response = pending.get();
  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, request);
}

TEST_F(CommFactorySerialCacheTest, ConflictingBaudRateIsRejectedAndOriginalSerialIsPreserved) {
  auto first = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(first.ok()) << first.status();

  auto conflicting = comm_;
  conflicting.mutable_serial_config()->set_baudrate(9600);
  auto rejected = CommFactory::CreateComm(conflicting);
  ASSERT_EQ(rejected.status().code(), absl::StatusCode::kInvalidArgument);
  const std::string error(rejected.status().message());
  EXPECT_NE(error.find(comm_.serial_config().port()), std::string::npos);
  EXPECT_NE(error.find("115200"), std::string::npos);
  EXPECT_NE(error.find("9600"), std::string::npos);

  auto retry = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(retry.ok()) << retry.status();
  auto first_stream = GetCommTransport<ByteStream>(*first);
  auto retry_stream = GetCommTransport<ByteStream>(*retry);
  ASSERT_TRUE(first_stream.ok());
  ASSERT_TRUE(retry_stream.ok());
  EXPECT_EQ(*first_stream, *retry_stream);
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
  auto comm = MakeEthercatComm();
  auto first_or = CommFactory::CreateEthercat(comm.ethercat_config());
  auto second_or = CommFactory::CreateEthercat(comm.ethercat_config());

  ASSERT_TRUE(first_or.ok()) << first_or.status();
  ASSERT_TRUE(second_or.ok()) << second_or.status();
  EXPECT_EQ(first_or->get(), second_or->get());
}

TEST_F(CommFactoryEthercatCacheTest, TiDemoDoesNotAdvertiseCorrelatedCyclicOrMailbox) {
  auto selected = CommFactory::CreateComm(MakeEthercatComm());
  ASSERT_TRUE(selected.ok());
  EXPECT_TRUE(GetCommTransport<ethercat::EthercatTransport>(*selected).ok());
  EXPECT_EQ(GetCommTransport<CorrelatedCyclicTransport>(*selected).status().code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(GetCommTransport<MessageTransport>(*selected).status().code(),
            absl::StatusCode::kInvalidArgument);
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
