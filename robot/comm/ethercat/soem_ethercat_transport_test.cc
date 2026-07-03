#include "robot/comm/ethercat/soem_ethercat_transport.h"

#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {
namespace {

TEST(SoemEthercatTransportTest, InitRejectsEmptyInterfaceName) {
  SoemEthercatTransport transport;

  auto status = transport.Init("", ProcessDataMode::kSplitLrdLwr);

  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SoemEthercatTransportTest, InitReturnsUnimplementedUntilSoemBackendExists) {
  SoemEthercatTransport transport;

  auto status = transport.Init("enp5s0", ProcessDataMode::kSplitLrdLwr);

  EXPECT_EQ(status.code(), absl::StatusCode::kUnimplemented);
}

TEST(SoemEthercatTransportTest, PdoOperationsReturnUnimplementedUntilBackendExists) {
  SoemEthercatTransport transport;
  PdoRegion region;
  region.slave_index = 2;
  region.output_size_bytes = 8;
  region.input_size_bytes = 8;

  EXPECT_EQ(transport.ConfigureSlaves().code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport.StartCyclic().code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport.StopCyclic().code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport.Teardown().code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport.GetSlaves().status().code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport.GetPdoRegion(2).status().code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport.WriteOutputs(region, std::vector<uint8_t>(8, 0)).code(),
            absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport.ReadInputs(region).status().code(), absl::StatusCode::kUnimplemented);
  EXPECT_EQ(transport.ExchangeProcessData().status().code(), absl::StatusCode::kUnimplemented);
}

}  // namespace
}  // namespace robot::comm::ethercat
