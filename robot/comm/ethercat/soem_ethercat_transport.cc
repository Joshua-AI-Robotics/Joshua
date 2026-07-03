#include "robot/comm/ethercat/soem_ethercat_transport.h"

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "soem/soem.h"

namespace robot::comm::ethercat {

namespace {

absl::Status SoemUnimplementedStatus(const std::string& operation) {
  return absl::Status(absl::StatusCode::kUnimplemented,
                      "SOEM EtherCAT transport is not implemented for " + operation);
}

}  // namespace

absl::Status SoemEthercatTransport::Init(const std::string& interface_name,
                                         ProcessDataMode process_data_mode) {
  if (interface_name.empty()) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "SOEM EtherCAT interface name is empty");
  }
  interface_name_ = interface_name;
  process_data_mode_ = process_data_mode;
  return SoemUnimplementedStatus("Init");
}

absl::Status SoemEthercatTransport::ConfigureSlaves() {
  return SoemUnimplementedStatus("ConfigureSlaves");
}

absl::Status SoemEthercatTransport::StartCyclic() {
  return SoemUnimplementedStatus("StartCyclic");
}

absl::Status SoemEthercatTransport::StopCyclic() {
  return SoemUnimplementedStatus("StopCyclic");
}

absl::Status SoemEthercatTransport::Teardown() {
  return SoemUnimplementedStatus("Teardown");
}

absl::StatusOr<std::vector<SlaveIdentity>> SoemEthercatTransport::GetSlaves() const {
  return SoemUnimplementedStatus("GetSlaves");
}

absl::StatusOr<PdoRegion> SoemEthercatTransport::GetPdoRegion(uint16_t slave_index) const {
  return SoemUnimplementedStatus("GetPdoRegion");
}

absl::Status SoemEthercatTransport::WriteOutputs(const PdoRegion& region,
                                                 const std::vector<uint8_t>& outputs) {
  return SoemUnimplementedStatus("WriteOutputs");
}

absl::StatusOr<std::vector<uint8_t>> SoemEthercatTransport::ReadInputs(
    const PdoRegion& region) const {
  return SoemUnimplementedStatus("ReadInputs");
}

absl::StatusOr<ProcessData> SoemEthercatTransport::ExchangeProcessData() {
  return SoemUnimplementedStatus("ExchangeProcessData");
}

}  // namespace robot::comm::ethercat
