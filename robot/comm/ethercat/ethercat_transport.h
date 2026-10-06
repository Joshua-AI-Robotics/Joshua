#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/ethercat/ethercat_types.h"

namespace robot::comm::ethercat {

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
