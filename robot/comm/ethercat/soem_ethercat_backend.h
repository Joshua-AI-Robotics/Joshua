#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/ethercat/ethercat_master.h"
#include "robot/comm/ethercat/ethercat_types.h"

namespace robot::comm::ethercat {

// SOEM-backed EtherCAT I/O, owned exclusively by EthercatMaster.
//
// Synchronous low-level I/O, never a board-facing transport. SOEM types
// never cross this boundary. Blocking SDO methods close at cyclic startup;
// runtime SDO uses only the budgeted Begin/Step/Cancel path.
class SoemEthercatBackend : public EthercatMasterIo {
 public:
  SoemEthercatBackend();
  ~SoemEthercatBackend() override;

  absl::Status Init(const std::string& interface_name, ProcessDataMode process_data_mode) override;
  absl::Status ConfigureSlaves() override;
  absl::Status StartCyclic() override;
  absl::Status StopCyclic() override;
  absl::Status Teardown() override;

  absl::StatusOr<std::vector<SlaveIdentity>> GetSlaves() const override;
  absl::StatusOr<PdoRegion> GetPdoRegion(uint16_t slave_index) const override;

  absl::Status WriteOutputs(const PdoRegion& region, const std::vector<uint8_t>& outputs) override;
  absl::StatusOr<ProcessData> ExchangeProcessData(int timeout_us) override;
  absl::Status CheckOperational(int timeout_us) override;
  absl::StatusOr<std::vector<uint8_t>> ReadSdo(SdoAddress address,
                                               size_t capacity,
                                               int timeout_us) override;
  absl::Status WriteSdo(SdoAddress address,
                        const std::vector<uint8_t>& bytes,
                        int timeout_us) override;
  bool HasIncrementalSdo() const override {
    return true;
  }
  absl::Status BeginSdo(SdoAddress address,
                        bool write,
                        std::vector<uint8_t> bytes,
                        size_t capacity) override;
  absl::StatusOr<std::optional<std::vector<uint8_t>>> StepSdo(int budget_us) override;
  void CancelSdo() override;

 private:
  struct State;
  absl::Status ValidateSdo(SdoAddress address,
                           size_t size,
                           int timeout_us,
                           bool incremental = false) const;

  std::string interface_name_;
  ProcessDataMode process_data_mode_ = ProcessDataMode::kSplitLrdLwr;
  std::unique_ptr<State> state_;
};

}  // namespace robot::comm::ethercat
