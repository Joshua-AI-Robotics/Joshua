#include "robot/comm/ethercat/ethercat_status.h"

#include "absl/status/status.h"
#include "gtest/gtest.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {
namespace {

TEST(EthercatStatusTest, ValidatePdoRegionAcceptsNonZeroInputAndOutputSizes) {
  PdoRegion region;
  region.output_size_bytes = 8;
  region.input_size_bytes = 8;

  EXPECT_TRUE(ValidatePdoRegion(region).ok());
}

TEST(EthercatStatusTest, ValidatePdoRegionRejectsZeroOutputSize) {
  PdoRegion region;
  region.output_size_bytes = 0;
  region.input_size_bytes = 8;

  EXPECT_EQ(ValidatePdoRegion(region).code(), absl::StatusCode::kInvalidArgument);
}

TEST(EthercatStatusTest, ValidatePdoRegionRejectsZeroInputSize) {
  PdoRegion region;
  region.output_size_bytes = 8;
  region.input_size_bytes = 0;

  EXPECT_EQ(ValidatePdoRegion(region).code(), absl::StatusCode::kInvalidArgument);
}

TEST(EthercatStatusTest, ValidateWorkingCountAcceptsExpectedCount) {
  EXPECT_TRUE(ValidateWorkingCount(3, 3).ok());
}

TEST(EthercatStatusTest, ValidateWorkingCountRejectsNegativeExpectedCount) {
  EXPECT_EQ(ValidateWorkingCount(3, -1).code(), absl::StatusCode::kInvalidArgument);
}

TEST(EthercatStatusTest, ValidateWorkingCountRejectsMismatch) {
  EXPECT_EQ(ValidateWorkingCount(-1, 3).code(), absl::StatusCode::kUnavailable);
}

TEST(EthercatStatusTest, ValidateProcessDataChecksWorkingCount) {
  ProcessData process_data;
  process_data.expected_working_count = 3;
  process_data.working_count = 3;

  EXPECT_TRUE(ValidateProcessData(process_data).ok());

  process_data.working_count = -1;
  EXPECT_EQ(ValidateProcessData(process_data).code(), absl::StatusCode::kUnavailable);
}

}  // namespace
}  // namespace robot::comm::ethercat
