#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/ethercat/ethercat_master.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {

// SOEM-backed EtherCAT transport.
//
// Synchronous low-level I/O: retained TI-demo callers serialize it themselves.
// For the new path, transfer exclusive ownership to EthercatMaster. SOEM types
// never cross this boundary. SDO methods are forbidden after cyclic startup.
class SoemEthercatTransport : public EthercatMasterIo {
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
  absl::StatusOr<ProcessData> ExchangeProcessData(int timeout_us) override;
  absl::Status CheckOperational(int timeout_us) override;
  absl::StatusOr<std::vector<uint8_t>> ReadSdo(SdoAddress address,
                                               size_t capacity,
                                               int timeout_us) override;
  absl::Status WriteSdo(SdoAddress address,
                        const std::vector<uint8_t>& bytes,
                        int timeout_us) override;

 private:
  struct State;
  absl::Status ValidateSdo(SdoAddress address, size_t size, int timeout_us) const;

  std::string interface_name_;
  ProcessDataMode process_data_mode_ = ProcessDataMode::kSplitLrdLwr;
  std::unique_ptr<State> state_;
};

}  // namespace robot::comm::ethercat
