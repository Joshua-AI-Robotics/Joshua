#include "robot/comm/factory/comm_factory.h"

#include <fcntl.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <climits>
#include <memory>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/proto/comm.pb.h"
#include "robot/comm/testing/fake_transports.h"

namespace robot::comm {
namespace {

robot::comm::Comm MakeEthercatComm() {
  robot::comm::Comm comm;
  comm.set_comm_type(robot::comm::CommType::ETHERCAT);
  comm.set_transport_type(robot::comm::TransportType::MESSAGE_AND_CYCLIC);
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
  comm.set_transport_type(robot::comm::TransportType::MESSAGE_AND_CYCLIC);

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
  selected = PairedTransports{message, cyclic, absl::Milliseconds(100)};
  ASSERT_TRUE(GetCommTransport<MessageTransport>(selected).ok());
  ASSERT_TRUE(GetCommTransport<CorrelatedCyclicTransport>(selected).ok());
  EXPECT_EQ(GetCommTransport<ByteStream>(selected).status().code(),
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

TEST(CommFactoryTest, LegacyEthercatIsRejectedBeforeOpeningBackend) {
  auto comm = MakeEthercatComm();
  comm.set_transport_type(CYCLIC);
  auto result = CommFactory::CreateComm(comm);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
  EXPECT_NE(result.status().message().find("retired"), std::string::npos);
}

TEST(CommFactoryTest, PairedEthercatRejectsIncompleteConfigBeforeOpeningBackend) {
  auto result = CommFactory::CreateComm(MakeEthercatComm());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, InvalidSerialTimingIsRejectedBeforeOpeningPort) {
  Comm comm;
  comm.set_comm_type(SERIAL);
  comm.set_transport_type(MESSAGE);
  auto* config = comm.mutable_serial_config();
  config->set_port("never-open-this-port");
  config->set_baudrate(115200);
  EXPECT_TRUE(CommFactory::ValidateSerialConfig(*config).ok());
  config->set_exchange_timeout_ms(0);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
  config->set_exchange_timeout_ms(UINT32_MAX);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
  config->clear_exchange_timeout_ms();
  config->set_post_open_settle_ms(UINT32_MAX);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
  config->clear_post_open_settle_ms();
  config->set_baudrate(UINT64_MAX);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(CommFactoryTest, SerialPtyHonorsTimingAndSharesOnePhysicalOpen) {
  const int master = posix_openpt(O_RDWR | O_NOCTTY);
  ASSERT_GE(master, 0);
  struct CloseFd {
    int fd;
    ~CloseFd() {
      close(fd);
    }
  } cleanup{master};
  ASSERT_EQ(grantpt(master), 0);
  ASSERT_EQ(unlockpt(master), 0);
  Comm comm;
  comm.set_comm_type(SERIAL);
  comm.set_transport_type(MESSAGE);
  auto* config = comm.mutable_serial_config();
  config->set_port(ptsname(master));
  config->set_baudrate(115200);
  config->set_post_open_settle_ms(60);
  config->set_exchange_timeout_ms(30);
  const auto opened = std::chrono::steady_clock::now();
  auto selected = CommFactory::CreateComm(comm);
  ASSERT_TRUE(selected.ok()) << selected.status();
  EXPECT_GE(std::chrono::steady_clock::now() - opened, std::chrono::milliseconds(60));
  auto message = GetCommTransport<MessageTransport>(*selected);
  ASSERT_TRUE(message.ok());
  const std::vector<uint8_t> frame = {0xa5, 3, 1, 1, 0, 0, 0};
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ((*message)->Exchange(frame).status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(30));
  comm.set_transport_type(BYTE_STREAM);
  auto first = CommFactory::CreateComm(comm);
  auto second = CommFactory::CreateComm(comm);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(*GetCommTransport<ByteStream>(*first), *GetCommTransport<ByteStream>(*second));
  config->set_exchange_timeout_ms(31);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
  config->set_exchange_timeout_ms(30);
  config->set_post_open_settle_ms(0);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
  config->set_post_open_settle_ms(60);
  config->set_baudrate(9600);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace robot::comm
