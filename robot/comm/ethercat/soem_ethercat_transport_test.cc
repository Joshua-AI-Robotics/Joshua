#include "robot/comm/ethercat/soem_ethercat_transport.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/ethercat/ethercat_master.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {
namespace {

TEST(SoemEthercatTransportTest, InitRejectsEmptyInterfaceName) {
  SoemEthercatTransport transport;

  auto status = transport.Init("", ProcessDataMode::kSplitLrdLwr);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SoemEthercatTransportTest, InitRejectsLrwMode) {
  SoemEthercatTransport transport;

  auto status = transport.Init("enp5s0", ProcessDataMode::kLrw);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SoemEthercatTransportTest, InitReportsUnavailableForMissingInterface) {
  SoemEthercatTransport transport;

  auto status = transport.Init("joshua-no-such-ethercat-iface0", ProcessDataMode::kSplitLrdLwr);

  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
}

TEST(SoemEthercatTransportTest, TeardownIsOkBeforeInit) {
  SoemEthercatTransport transport;

  EXPECT_TRUE(transport.Teardown().ok());
}

TEST(SoemEthercatTransportTest, ConfigureSlavesRequiresInit) {
  SoemEthercatTransport transport;

  EXPECT_EQ(transport.ConfigureSlaves().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, StartCyclicRequiresConfiguredSlaves) {
  SoemEthercatTransport transport;

  EXPECT_EQ(transport.StartCyclic().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, MetadataAccessRequiresConfiguredSlaves) {
  SoemEthercatTransport transport;

  EXPECT_EQ(transport.GetSlaves().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.GetPdoRegion(2).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, PdoBufferAccessRequiresConfiguredSlaves) {
  SoemEthercatTransport transport;
  PdoRegion region;
  region.slave_index = 2;
  region.output_size_bytes = 8;
  region.input_size_bytes = 8;

  EXPECT_EQ(transport.WriteOutputs(region, std::vector<uint8_t>(8, 0)).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.ReadInputs(region).status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, ExchangeProcessDataRequiresConfiguredSlaves) {
  SoemEthercatTransport transport;

  EXPECT_EQ(transport.ExchangeProcessData().status().code(), absl::StatusCode::kFailedPrecondition);
}

TEST(SoemEthercatTransportTest, StopCyclicIsOkBeforeInit) {
  SoemEthercatTransport transport;

  EXPECT_TRUE(transport.StopCyclic().ok());
}

TEST(SoemEthercatTransportTest, NewIoMethodsFailBeforeAccessingHardware) {
  SoemEthercatTransport transport;
  EXPECT_EQ(transport.ReadSdo({1, 0x2000, 0}, 36, 1000).status().code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.WriteSdo({1, 0x2001, 0}, {1}, 1000).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.CheckOperational(1000).code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(transport.ExchangeProcessData(0).status().code(), absl::StatusCode::kInvalidArgument);
}

// Owner-loop regressions stay alongside backend tests. This test-local backend
// never opens a NIC; finite gates simulate slow I/O without risking hung tests.
using namespace std::chrono_literals;

}  // namespace
}  // namespace robot::comm::ethercat
