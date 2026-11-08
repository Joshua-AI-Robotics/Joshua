// Exercise the production backend with memory-only SOEM discovery/mapping.
// Link wrappers replace every SOEM call reached here; no NIC or socket opens.
#include <vector>

#include "gtest/gtest.h"
#include "robot/comm/ethercat/soem_ethercat_backend.h"
#include "soem/soem.h"

namespace {
struct Mapping {
  int slaves = 1;
  uint32_t output_bytes = 112;
  uint32_t input_bytes = 112;
  uint32_t mailbox_bytes = 0;
  int dc_calls = 0;
} mapping;
}  // namespace

extern "C" int __wrap_ecx_init(ecx_contextt*, const char*) {
  return 1;
}
extern "C" void __wrap_ecx_close(ecx_contextt*) {}
extern "C" int __wrap_ecx_config_init(ecx_contextt* context) {
  context->slavecount = mapping.slaves;
  for (int i = 1; i <= context->slavecount; ++i) {
    auto& slave = context->slavelist[i];
    slave.eep_man = 1;  // Avoid the zero-identity retry path.
    slave.Obytes = mapping.output_bytes / mapping.slaves;
    slave.Ibytes = mapping.input_bytes / mapping.slaves;
    slave.Obits = slave.Obytes * 8;
    slave.Ibits = slave.Ibytes * 8;
  }
  return context->slavecount;
}
extern "C" int __wrap_ecx_config_map_group(ecx_contextt* context, void* iomap, uint8 group) {
  auto& mapped = context->grouplist[group];
  mapped.outputs = static_cast<uint8_t*>(iomap);
  mapped.Obytes = mapping.output_bytes;
  mapped.inputs = mapped.outputs + mapped.Obytes;
  mapped.Ibytes = mapping.input_bytes;
  mapped.mbxstatuslength = mapping.mailbox_bytes;
  for (int i = 1; i <= context->slavecount; ++i) {
    auto& slave = context->slavelist[i];
    slave.outputs = mapped.outputs + (i - 1) * slave.Obytes;
    slave.inputs = mapped.inputs + (i - 1) * slave.Ibytes;
  }
  return mapped.Obytes + mapped.Ibytes + mapped.mbxstatuslength;
}
extern "C" boolean __wrap_ecx_configdc(ecx_contextt*) {
  ++mapping.dc_calls;
  return TRUE;
}

namespace robot::comm::ethercat {
namespace {
class SoemEthercatMappingTest : public ::testing::Test {
 protected:
  void SetUp() override {
    mapping = Mapping{};
    ASSERT_TRUE(backend.Init("memory-only-no-nic", ProcessDataMode::kSplitLrdLwr).ok());
  }
  SoemEthercatBackend backend;
};

TEST_F(SoemEthercatMappingTest, ExactCapacityMapCanCopyItsLastOutputByte) {
  mapping.output_bytes = 4096;
  mapping.input_bytes = 0;
  ASSERT_TRUE(backend.ConfigureSlaves().ok());
  auto region = backend.GetPdoRegion(1);
  ASSERT_TRUE(region.ok());
  EXPECT_TRUE(backend.WriteOutputs(*region, std::vector<uint8_t>(4096, 0x5a)).ok());
  EXPECT_EQ(mapping.dc_calls, 1);
}

TEST_F(SoemEthercatMappingTest, NineteenJoshuaWireSlavesExceedMapCapacity) {
  mapping.slaves = 19;
  mapping.output_bytes = mapping.input_bytes = 19 * 112;
  mapping.mailbox_bytes = 19;
  const auto status = backend.ConfigureSlaves();
  ASSERT_EQ(status.code(), absl::StatusCode::kResourceExhausted);
  EXPECT_NE(status.message().find("4275"), std::string::npos);
  EXPECT_NE(status.message().find("4096"), std::string::npos);
  EXPECT_EQ(mapping.dc_calls, 0);
  EXPECT_EQ(backend.GetSlaves().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(backend.GetPdoRegion(19).status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(backend.StartCyclic().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ(backend.WriteOutputs({19, 2016, 2016, 112, 112}, std::vector<uint8_t>(112)).code(),
            absl::StatusCode::kFailedPrecondition);
}

TEST_F(SoemEthercatMappingTest, InputAndMailboxBytesCountTowardCapacity) {
  mapping.output_bytes = 2048;
  mapping.input_bytes = 2048;
  mapping.mailbox_bytes = 1;
  EXPECT_EQ(backend.ConfigureSlaves().code(), absl::StatusCode::kResourceExhausted);
  EXPECT_EQ(mapping.dc_calls, 0);
}
}  // namespace
}  // namespace robot::comm::ethercat
