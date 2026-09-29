#include "robot/comm/factory/comm_factory.h"

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

}  // namespace
}  // namespace robot::comm
