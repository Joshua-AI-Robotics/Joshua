#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {

class FakeEthercatTransport : public EthercatTransport {
 public:
  absl::Status Init(const std::string& interface_name, ProcessDataMode process_data_mode) override {
    interface_name_ = interface_name;
    process_data_mode_ = process_data_mode;
    return absl::OkStatus();
  }

  absl::Status ConfigureSlaves() override {
    return absl::OkStatus();
  }
  absl::Status StartCyclic() override {
    return absl::OkStatus();
  }
  absl::Status StopCyclic() override {
    return absl::OkStatus();
  }
  absl::Status Teardown() override {
    return absl::OkStatus();
  }

  absl::StatusOr<std::vector<SlaveIdentity>> GetSlaves() const override {
    return slaves_;
  }

  absl::StatusOr<PdoRegion> GetPdoRegion(uint16_t slave_index) const override {
    get_pdo_region_calls_++;
    requested_slave_index_ = slave_index;
    if (!get_pdo_region_status_.ok()) {
      return get_pdo_region_status_;
    }
    return pdo_region_;
  }

  absl::Status WriteOutputs(const PdoRegion& region, const std::vector<uint8_t>& outputs) override {
    last_write_region_ = region;
    last_outputs_ = outputs;
    return write_outputs_status_;
  }

  absl::StatusOr<std::vector<uint8_t>> ReadInputs(const PdoRegion& region) const override {
    last_read_region_ = region;
    return inputs_;
  }

  absl::StatusOr<ProcessData> ExchangeProcessData() override {
    exchange_process_data_calls_++;
    if (!exchange_process_data_status_.ok()) {
      return exchange_process_data_status_;
    }
    return process_data_;
  }

  mutable int get_pdo_region_calls_ = 0;
  mutable uint16_t requested_slave_index_ = 0;
  int exchange_process_data_calls_ = 0;
  std::string interface_name_;
  ProcessDataMode process_data_mode_ = ProcessDataMode::kSplitLrdLwr;
  PdoRegion pdo_region_ = [] {
    PdoRegion region;
    region.slave_index = 2;
    region.output_offset_bytes = 4;
    region.input_offset_bytes = 12;
    region.output_size_bytes = 8;
    region.input_size_bytes = 8;
    return region;
  }();
  absl::Status get_pdo_region_status_ = absl::OkStatus();
  absl::Status write_outputs_status_ = absl::OkStatus();
  absl::Status exchange_process_data_status_ = absl::OkStatus();
  std::vector<SlaveIdentity> slaves_;
  std::vector<uint8_t> inputs_;
  ProcessData process_data_;
  PdoRegion last_write_region_;
  mutable PdoRegion last_read_region_;
  std::vector<uint8_t> last_outputs_;
};

}  // namespace robot::comm::ethercat
