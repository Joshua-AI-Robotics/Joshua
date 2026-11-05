#include "robot/comm/factory/comm_factory.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/interfaces/caller_sized_message_transport.h"
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

TEST(CommFactoryTest, InvalidSerialAdapterSettingsAreRejectedBeforeOpeningDevices) {
  Comm comm;
  comm.set_comm_type(SERIAL);
  comm.set_transport_type(MESSAGE);
  auto* serial = comm.mutable_serial_config();
  serial->set_port("never-open-this-port");
  serial->set_baudrate(115200);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
  serial->set_message_adapter(static_cast<SerialMessageAdapter>(999));
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
  serial->set_message_adapter(JOSHUA_WIRE);
  for (auto timeout : {0u, std::numeric_limits<uint32_t>::max()}) {
    serial->set_exchange_timeout_ms(timeout);
    EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
  }
  serial->set_message_adapter(CALLER_SIZED);
  serial->set_exchange_timeout_ms(100);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
  serial->clear_exchange_timeout_ms();
  comm.set_transport_type(BYTE_STREAM);
  EXPECT_EQ(CommFactory::CreateComm(comm).status().code(), absl::StatusCode::kInvalidArgument);
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

TEST_F(CommFactorySerialCacheTest, AliasesShareOwnerAndLastLeaseRetiresGeneration) {
  uint64_t generation;
  const auto alias = std::string(::testing::TempDir()) + "serial-alias-" + std::to_string(getpid());
  std::error_code error;
  std::filesystem::create_symlink(comm_.serial_config().port(), alias, error);
  ASSERT_FALSE(error) << error.message();
  {
    auto first = CommFactory::CreateComm(comm_);
    ASSERT_TRUE(first.ok()) << first.status();
    auto config = comm_;
    config.mutable_serial_config()->set_port(alias);
    auto second = CommFactory::CreateComm(config);
    std::filesystem::remove(alias, error);
    ASSERT_TRUE(second.ok()) << second.status();
    EXPECT_EQ(*GetCommTransport<ByteStream>(*first), *GetCommTransport<ByteStream>(*second));
    auto snapshots = CommFactory::SnapshotSerialConnections();
    ASSERT_EQ(snapshots.size(), 1);
    generation = snapshots.front().generation;
    EXPECT_EQ(snapshots.front().configured_port, comm_.serial_config().port());
    EXPECT_EQ(snapshots.front().state, SerialLinkState::kReady);
  }
  EXPECT_TRUE(CommFactory::SnapshotSerialConnections().empty());
  auto reopened = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(reopened.ok()) << reopened.status();
  EXPECT_GT(CommFactory::SnapshotSerialConnections().front().generation, generation);
}

TEST_F(CommFactorySerialCacheTest, EffectiveIoPolicyMustAgreeWithoutReplacingLiveOwner) {
  auto first = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(first.ok()) << first.status();
  auto config = comm_;
  auto* policy = config.mutable_serial_config()->mutable_io_policy();
  policy->set_read_timeout_ms(500);  // An explicit default matches an omitted field.
  auto matching = CommFactory::CreateComm(config);
  ASSERT_TRUE(matching.ok()) << matching.status();
  EXPECT_EQ(*GetCommTransport<ByteStream>(*first), *GetCommTransport<ByteStream>(*matching));
  policy->set_read_timeout_ms(501);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(config).status()));
  policy->set_read_timeout_ms(500);
  policy->set_max_pending_operations(15);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(config).status()));
  EXPECT_EQ(CommFactory::SnapshotSerialConnections().size(), 1);
}

TEST_F(CommFactorySerialCacheTest, ContinuousFramesShareOneLeaseAndRejectRequestOnlyUsers) {
  comm_.set_transport_type(FRAME_STREAM);
  comm_.mutable_serial_config()->set_message_adapter(JOSHUA_WIRE);
  auto first = CommFactory::CreateComm(comm_);
  auto second = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(first.ok()) << first.status();
  ASSERT_TRUE(second.ok()) << second.status();
  auto frames = GetCommTransport<FrameStreamTransport>(*first);
  ASSERT_TRUE(frames.ok()) << frames.status();
  EXPECT_EQ(*frames, *GetCommTransport<FrameStreamTransport>(*second));
  EXPECT_TRUE(absl::IsInvalidArgument(GetCommTransport<MessageTransport>(*first).status()));
  auto conflicting = comm_;
  conflicting.set_transport_type(MESSAGE);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(conflicting).status()));
  conflicting.set_transport_type(BYTE_STREAM);
  conflicting.mutable_serial_config()->clear_message_adapter();
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(conflicting).status()));
  EXPECT_EQ(CommFactory::SnapshotSerialConnections().size(), 1);
  auto policy = comm_;
  policy.mutable_serial_config()->mutable_frame_policy()->set_max_reply_frames(4);
  EXPECT_TRUE(CommFactory::CreateComm(policy).ok());  // Explicit default matches.
  policy.mutable_serial_config()->mutable_frame_policy()->set_max_reply_frames(5);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(policy).status()));
}

TEST_F(CommFactorySerialCacheTest, ContinuousReceiverCannotJoinAnExistingRawConnection) {
  auto raw = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(raw.ok()) << raw.status();
  auto frames = comm_;
  frames.set_transport_type(FRAME_STREAM);
  frames.mutable_serial_config()->set_message_adapter(JOSHUA_WIRE);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(frames).status()));
  EXPECT_TRUE(GetCommTransport<ByteStream>(*raw)->get()->Open().ok());
}

TEST_F(CommFactorySerialCacheTest, FrameAliasesShareIdentityAndLastLeaseReleasesReceiver) {
  comm_.set_transport_type(FRAME_STREAM);
  comm_.mutable_serial_config()->set_message_adapter(JOSHUA_WIRE);
  uint64_t generation;
  const auto alias = std::string(::testing::TempDir()) + "frame-alias-" + std::to_string(getpid());
  std::error_code error;
  std::filesystem::create_symlink(comm_.serial_config().port(), alias, error);
  ASSERT_FALSE(error) << error.message();
  {
    auto first = CommFactory::CreateComm(comm_);
    ASSERT_TRUE(first.ok()) << first.status();
    auto config = comm_;
    config.mutable_serial_config()->set_port(alias);
    auto second = CommFactory::CreateComm(config);
    std::filesystem::remove(alias, error);
    ASSERT_TRUE(second.ok()) << second.status();
    EXPECT_EQ(*GetCommTransport<FrameStreamTransport>(*first),
              *GetCommTransport<FrameStreamTransport>(*second));
    generation = CommFactory::SnapshotSerialConnections().front().generation;
  }
  EXPECT_TRUE(CommFactory::SnapshotSerialConnections().empty());
  auto reopened = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(reopened.ok()) << reopened.status();
  EXPECT_GT(CommFactory::SnapshotSerialConnections().front().generation, generation);
}

TEST(CommFactoryTest, FrameStreamPolicyIsValidatedWithoutOpeningHardware) {
  Comm comm;
  comm.set_comm_type(SERIAL);
  comm.set_transport_type(FRAME_STREAM);
  auto* config = comm.mutable_serial_config();
  config->set_port("never-open-this-device");
  config->set_baudrate(115200);
  config->set_message_adapter(CALLER_SIZED);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(comm).status()));
  config->set_message_adapter(JOSHUA_WIRE);
  config->set_exchange_timeout_ms(100);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(comm).status()));
  config->clear_exchange_timeout_ms();
  config->mutable_frame_policy()->set_max_reply_frames(0);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(comm).status()));
  config->mutable_frame_policy()->set_max_reply_frames(4);
  config->mutable_frame_policy()->set_max_notification_frames(257);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(comm).status()));
  config->mutable_frame_policy()->set_max_notification_frames(32);
  config->mutable_frame_policy()->set_frame_idle_timeout_ms(0);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(comm).status()));
  config->mutable_frame_policy()->set_frame_idle_timeout_ms(100);
  config->mutable_io_policy()->set_max_transfer_bytes(95);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(comm).status()));
  config->mutable_io_policy()->set_max_transfer_bytes(96);
  comm.set_transport_type(MESSAGE);
  EXPECT_TRUE(absl::IsInvalidArgument(CommFactory::CreateComm(comm).status()));
}

TEST_F(CommFactorySerialCacheTest, MessageAdaptersShareOneSerialBusLock) {
  comm_.set_transport_type(MESSAGE);
  comm_.mutable_serial_config()->set_message_adapter(JOSHUA_WIRE);
  comm_.mutable_serial_config()->set_exchange_timeout_ms(500);
  auto first = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(first.ok()) << first.status();
  auto first_message = GetCommTransport<MessageTransport>(*first);
  ASSERT_TRUE(first_message.ok());
  auto second = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(second.ok()) << second.status();
  auto second_message = GetCommTransport<MessageTransport>(*second);
  ASSERT_TRUE(second_message.ok());

  const std::vector<uint8_t> request{
      0xa5, 0x0b, 0x02, 0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01, 0x08, 0xff, 0x82, 0x0c};
  auto pending =
      std::async(std::launch::async, [&] { return (*first_message)->Exchange(request); });
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
  auto queued =
      std::async(std::launch::async, [&] { return (*second_message)->Exchange(request); });
  EXPECT_EQ(queued.wait_for(std::chrono::milliseconds(25)), std::future_status::timeout);
  EXPECT_EQ(poll(&descriptor, 1, 10), 0);

  ASSERT_EQ(write(master_fd_, request.data(), request.size()),
            static_cast<ssize_t>(request.size()));
  auto response = pending.get();
  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, request);

  received = 0;
  while (received < observed.size()) {
    ASSERT_GT(poll(&descriptor, 1, 1000), 0);
    const ssize_t count = read(master_fd_, observed.data() + received, observed.size() - received);
    ASSERT_GT(count, 0);
    received += static_cast<size_t>(count);
  }
  ASSERT_EQ(observed, request);
  ASSERT_EQ(write(master_fd_, request.data(), request.size()),
            static_cast<ssize_t>(request.size()));
  auto queued_response = queued.get();
  ASSERT_TRUE(queued_response.ok()) << queued_response.status();
  EXPECT_EQ(*queued_response, request);
}

TEST_F(CommFactorySerialCacheTest, ConfiguredAdaptersExposeDifferentMessageContracts) {
  comm_.set_transport_type(MESSAGE);
  comm_.mutable_serial_config()->set_message_adapter(CALLER_SIZED);
  auto fixed = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(fixed.ok()) << fixed.status();
  auto fixed_message = GetCommTransport<MessageTransport>(*fixed);
  ASSERT_TRUE(fixed_message.ok());
  EXPECT_TRUE(GetCallerSizedMessageTransport(*fixed_message).ok());
  const std::vector<uint8_t> vendor_request{0xff, 0xff, 0x01, 0x02};
  EXPECT_EQ((*fixed_message)->Exchange(vendor_request).status().code(),
            absl::StatusCode::kUnimplemented);

  comm_.mutable_serial_config()->set_message_adapter(JOSHUA_WIRE);
  auto framed = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(framed.ok()) << framed.status();
  auto framed_message = GetCommTransport<MessageTransport>(*framed);
  ASSERT_TRUE(framed_message.ok());
  EXPECT_EQ(GetCallerSizedMessageTransport(*framed_message).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ((*framed_message)->Exchange(vendor_request).status().code(),
            absl::StatusCode::kInvalidArgument);
  pollfd descriptor{master_fd_, POLLIN, 0};
  EXPECT_EQ(poll(&descriptor, 1, 10), 0);
}

TEST_F(CommFactorySerialCacheTest, CallerSizedAdapterSharesBusLockWithJoshuaWireAdapter) {
  comm_.set_transport_type(MESSAGE);
  comm_.mutable_serial_config()->set_message_adapter(JOSHUA_WIRE);
  comm_.mutable_serial_config()->set_exchange_timeout_ms(500);
  auto framed = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(framed.ok()) << framed.status();
  auto message = GetCommTransport<MessageTransport>(*framed);
  ASSERT_TRUE(message.ok());

  comm_.mutable_serial_config()->set_message_adapter(CALLER_SIZED);
  comm_.mutable_serial_config()->clear_exchange_timeout_ms();
  auto fixed = CommFactory::CreateComm(comm_);
  ASSERT_TRUE(fixed.ok()) << fixed.status();
  auto fixed_message = GetCommTransport<MessageTransport>(*fixed);
  ASSERT_TRUE(fixed_message.ok());
  auto caller_sized = GetCallerSizedMessageTransport(*fixed_message);
  ASSERT_TRUE(caller_sized.ok());

  const std::vector<uint8_t> request{
      0xa5, 0x0b, 0x02, 0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01, 0x08, 0xff, 0x82, 0x0c};
  auto exchange = std::async(std::launch::async, [&] { return (*message)->Exchange(request); });
  std::vector<uint8_t> observed(request.size());
  size_t received = 0;
  pollfd descriptor{master_fd_, POLLIN, 0};
  while (received < observed.size()) {
    ASSERT_GT(poll(&descriptor, 1, 1000), 0);
    const ssize_t count = read(master_fd_, observed.data() + received, observed.size() - received);
    ASSERT_GT(count, 0);
    received += static_cast<size_t>(count);
  }
  EXPECT_EQ(observed, request);
  const std::vector<uint8_t> vendor_request{0xff, 0xff, 0x01, 0x02};
  auto writer =
      std::async(std::launch::async, [&] { return (*caller_sized)->Write(vendor_request); });
  EXPECT_EQ(writer.wait_for(std::chrono::milliseconds(20)), std::future_status::timeout);
  EXPECT_EQ(poll(&descriptor, 1, 10), 0);
  EXPECT_EQ(write(master_fd_, request.data(), request.size()),
            static_cast<ssize_t>(request.size()));
  auto response = exchange.get();
  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, request);
  EXPECT_TRUE(writer.get().ok());
  ASSERT_GT(poll(&descriptor, 1, 1000), 0);
  std::vector<uint8_t> vendor_observed(vendor_request.size());
  ASSERT_EQ(read(master_fd_, vendor_observed.data(), vendor_observed.size()),
            static_cast<ssize_t>(vendor_observed.size()));
  EXPECT_EQ(vendor_observed, vendor_request);
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
  config->set_message_adapter(JOSHUA_WIRE);
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
      CommFactory::ResetSerialTransportCacheForTesting();
      close(fd);
    }
  } cleanup{master};
  ASSERT_EQ(grantpt(master), 0);
  ASSERT_EQ(unlockpt(master), 0);
  Comm comm;
  comm.set_comm_type(SERIAL);
  comm.set_transport_type(MESSAGE);
  auto* config = comm.mutable_serial_config();
  config->set_message_adapter(JOSHUA_WIRE);
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
  const std::vector<uint8_t> frame{
      0xa5, 0x0b, 0x02, 0x78, 0x56, 0x34, 0x12, 0x04, 0x03, 0x02, 0x01, 0x08, 0xff, 0x82, 0x0c};
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ((*message)->Exchange(frame).status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_GE(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(30));
  comm.set_transport_type(BYTE_STREAM);
  config->clear_message_adapter();
  config->clear_exchange_timeout_ms();
  auto first = CommFactory::CreateComm(comm);
  auto second = CommFactory::CreateComm(comm);
  ASSERT_TRUE(first.ok());
  ASSERT_TRUE(second.ok());
  EXPECT_EQ(*GetCommTransport<ByteStream>(*first), *GetCommTransport<ByteStream>(*second));
  comm.set_transport_type(MESSAGE);
  config->set_message_adapter(JOSHUA_WIRE);
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

TEST(SerialPolicyValidationTest, RejectsInvalidLimitsWithoutOpeningHardware) {
  robot::comm::SerialConfig config;
  config.set_port("never-open-this-device");
  config.set_baudrate(115200);
  auto* policy = config.mutable_io_policy();
  policy->set_read_timeout_ms(0);
  EXPECT_TRUE(absl::IsInvalidArgument(robot::comm::CommFactory::ValidateSerialConfig(config)));
  policy->set_read_timeout_ms(UINT32_MAX);
  EXPECT_TRUE(absl::IsInvalidArgument(robot::comm::CommFactory::ValidateSerialConfig(config)));
  policy->clear_read_timeout_ms();
  policy->set_max_pending_operations(1025);
  EXPECT_TRUE(absl::IsInvalidArgument(robot::comm::CommFactory::ValidateSerialConfig(config)));
  policy->clear_max_pending_operations();
  policy->set_max_pending_bytes(10);
  EXPECT_TRUE(absl::IsInvalidArgument(robot::comm::CommFactory::ValidateSerialConfig(config)));
  policy->set_max_transfer_bytes(10);
  EXPECT_TRUE(robot::comm::CommFactory::ValidateSerialConfig(config).ok());
}
