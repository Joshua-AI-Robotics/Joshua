#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {

// SOEM-backed EtherCAT transport landing pad.
//
// This class intentionally does not link SOEM yet. It preserves the runtime
// boundary and the current kUnimplemented behavior until the real split LRD/LWR
// master implementation is added.
class SoemEthercatTransport : public EthercatTransport {
 public:
  SoemEthercatTransport() = default;
  ~SoemEthercatTransport() override = default;

  absl::Status Init(const std::string& interface_name, ProcessDataMode process_data_mode) override;
  absl::Status ConfigureSlaves() override;
  absl::Status StartCyclic() override;
  absl::Status StopCyclic() override;
  absl::Status Teardown() override;

  absl::StatusOr<std::vector<SlaveIdentity>> GetSlaves() const override;
  absl::StatusOr<PdoRegion> GetPdoRegion(uint16_t slave_index) const override;

  absl::Status WriteOutputs(const PdoRegion& region, const std::vector<uint8_t>& outputs) override;
  absl::StatusOr<std::vector<uint8_t>> ReadInputs(const PdoRegion& region) const override;
  absl::StatusOr<ProcessData> ExchangeProcessData() override;

 private:
  std::string interface_name_;
  ProcessDataMode process_data_mode_ = ProcessDataMode::kSplitLrdLwr;
};

}  // namespace robot::comm::ethercat
