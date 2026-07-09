#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {

// SOEM-backed EtherCAT transport.
//
// The transport keeps SOEM types out of public Joshua interfaces. The first
// runtime slice opens/closes the SOEM master socket; slave configuration and
// cyclic PDO exchange are added behind this boundary incrementally.
class SoemEthercatTransport : public EthercatTransport {
 public:
  SoemEthercatTransport();
  ~SoemEthercatTransport() override;

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
  struct State;

  std::string interface_name_;
  ProcessDataMode process_data_mode_ = ProcessDataMode::kSplitLrdLwr;
  std::unique_ptr<State> state_;
};

}  // namespace robot::comm::ethercat
