#include "robot/comm/ethercat/ethercat_status.h"

#include <string>

#include "absl/status/status.h"

namespace robot::comm::ethercat {

absl::Status ValidatePdoRegion(const PdoRegion& region) {
  if (region.output_size_bytes == 0) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "EtherCAT PDO region has zero output size");
  }
  if (region.input_size_bytes == 0) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "EtherCAT PDO region has zero input size");
  }
  return absl::OkStatus();
}

absl::Status ValidateWorkingCount(int actual_working_count, int expected_working_count) {
  if (expected_working_count < 0) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "EtherCAT expected working count must be non-negative");
  }
  if (actual_working_count != expected_working_count) {
    return absl::Status(absl::StatusCode::kUnavailable,
                        "EtherCAT working count mismatch: actual " +
                            std::to_string(actual_working_count) + ", expected " +
                            std::to_string(expected_working_count));
  }
  return absl::OkStatus();
}

absl::Status ValidateProcessData(const ProcessData& process_data) {
  return ValidateWorkingCount(process_data.working_count, process_data.expected_working_count);
}

}  // namespace robot::comm::ethercat
