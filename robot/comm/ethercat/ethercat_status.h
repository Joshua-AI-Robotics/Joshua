#pragma once

#include "absl/status/status.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {

absl::Status ValidatePdoRegion(const PdoRegion& region);
absl::Status ValidateWorkingCount(int actual_working_count, int expected_working_count);
absl::Status ValidateProcessData(const ProcessData& process_data);

}  // namespace robot::comm::ethercat
