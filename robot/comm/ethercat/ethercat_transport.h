#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace robot::comm::ethercat {

enum class ProcessDataMode {
  kSplitLrdLwr,
  kLrw,
};

struct SlaveIdentity {
  std::string name;
  uint32_t manufacturer = 0;
  uint32_t product_id = 0;
  uint32_t revision = 0;
  uint16_t output_size_bits = 0;
  uint16_t input_size_bits = 0;
};

struct PdoRegion {
  uint16_t slave_index = 0;
  size_t output_offset_bytes = 0;
  size_t input_offset_bytes = 0;
  size_t output_size_bytes = 0;
  size_t input_size_bytes = 0;
};

struct ProcessData {
  std::vector<uint8_t> outputs;
  std::vector<uint8_t> inputs;
  int expected_working_count = 0;
  int working_count = 0;
};

// Generic EtherCAT master-side transport boundary.
//
// Implementations own slave discovery, state transitions, cyclic exchange, and
// working-count validation. AM243 users of the current TI demo firmware should
// configure kSplitLrdLwr, not kLrw.
class EthercatTransport {
 public:
  virtual ~EthercatTransport() = default;

  virtual absl::Status Init(const std::string& interface_name,
                            ProcessDataMode process_data_mode) = 0;
  virtual absl::Status ConfigureSlaves() = 0;
  virtual absl::Status StartCyclic() = 0;
  virtual absl::Status StopCyclic() = 0;
  virtual absl::Status Teardown() = 0;

  virtual absl::StatusOr<std::vector<SlaveIdentity>> GetSlaves() const = 0;
  virtual absl::StatusOr<PdoRegion> GetPdoRegion(uint16_t slave_index) const = 0;

  virtual absl::Status WriteOutputs(const PdoRegion& region,
                                    const std::vector<uint8_t>& outputs) = 0;
  virtual absl::StatusOr<std::vector<uint8_t>> ReadInputs(const PdoRegion& region) const = 0;
  virtual absl::StatusOr<ProcessData> ExchangeProcessData() = 0;
};

}  // namespace robot::comm::ethercat
