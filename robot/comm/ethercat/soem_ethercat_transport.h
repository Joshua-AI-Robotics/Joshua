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
class SoemEthercatTransport : public EthercatTransport {
 public:
  SoemEthercatTransport();
  ~SoemEthercatTransport();

  absl::Status Init(const std::string& interface_name, ProcessDataMode process_data_mode);
  absl::Status ConfigureSlaves();
  absl::Status StartCyclic();
  absl::Status StopCyclic();
  absl::Status Teardown();

  absl::StatusOr<std::vector<SlaveIdentity>> GetSlaves() const;
  absl::StatusOr<PdoRegion> GetPdoRegion(uint16_t slave_index) const;

  absl::Status WriteOutputs(const PdoRegion& region, const std::vector<uint8_t>& outputs);
  absl::StatusOr<std::vector<uint8_t>> ReadInputs(const PdoRegion& region) const;
  absl::StatusOr<ProcessData> ExchangeProcessData();
  absl::StatusOr<ProcessData> ExchangeProcessData(int timeout_us);
  absl::Status CheckOperational(int timeout_us);
  absl::StatusOr<std::vector<uint8_t>> ReadSdo(SdoAddress address, size_t capacity, int timeout_us);
  absl::Status WriteSdo(SdoAddress address, const std::vector<uint8_t>& bytes, int timeout_us);

 private:
  struct State;
  absl::Status ValidateSdo(SdoAddress address, size_t size, int timeout_us) const;

  std::string interface_name_;
  ProcessDataMode process_data_mode_ = ProcessDataMode::kSplitLrdLwr;
  std::unique_ptr<State> state_;
};

}  // namespace robot::comm::ethercat
