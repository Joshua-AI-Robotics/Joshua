// Hardware-free field checks shared by config preflight and the runtime factory.
#pragma once

#include <algorithm>
#include <climits>

#include "absl/status/status.h"
#include "firmware/common/joshua_wire_ethercat.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::comm {

inline absl::Status ValidateSerialConfig(const SerialConfig& config) {
  if (config.port().empty() || config.baudrate() == 0 || config.baudrate() > INT_MAX)
    return absl::InvalidArgumentError("Serial requires a port and baudrate in 1..INT_MAX");
  if ((config.has_exchange_timeout_ms() &&
       (config.exchange_timeout_ms() == 0 || config.exchange_timeout_ms() > INT_MAX)) ||
      config.post_open_settle_ms() > INT_MAX)
    return absl::InvalidArgumentError(
        "Serial exchange_timeout_ms must be 1..INT_MAX; post_open_settle_ms must be 0..INT_MAX");
  return absl::OkStatus();
}

inline absl::Status ValidatePairedEthercatConfig(const EthercatConfig& config) {
  if (config.interface_name().empty() || config.slave_index() == 0 ||
      config.slave_index() > UINT16_MAX ||
      config.process_data_mode() != ETHERCAT_PROCESS_DATA_MODE_SPLIT_LRD_LWR)
    return absl::InvalidArgumentError(
        "Paired EtherCAT requires NIC, slave_index 1..65535 and split LRD/LWR");
  const auto& t = config.timing();
  for (uint32_t value : {t.period_us(),
                         t.process_timeout_us(),
                         t.state_timeout_us(),
                         t.operation_timeout_us(),
                         t.mailbox_step_budget_us(),
                         t.scheduling_guard_us(),
                         t.response_timeout_us()}) {
    if (value == 0 || value > INT_MAX)
      return absl::InvalidArgumentError(
          "All EtherCAT timing budgets must be explicit, 1..INT_MAX microseconds");
  }
  if (uint64_t(t.period_us()) <= uint64_t(t.process_timeout_us()) +
                                     std::max(t.state_timeout_us(), t.mailbox_step_budget_us()) +
                                     t.scheduling_guard_us() ||
      t.response_timeout_us() <= t.period_us() || t.operation_timeout_us() <= t.period_us())
    return absl::InvalidArgumentError(
        "EtherCAT timing budgets do not fit the cycle/response deadlines");
  if (config.has_pdo_region() && (config.pdo_region().output_size_bytes() != JWEC_PDO_SIZE ||
                                  config.pdo_region().input_size_bytes() != JWEC_PDO_SIZE))
    return absl::InvalidArgumentError("JW layout-v2 requires 112-byte PDO region assertions");
  return absl::OkStatus();
}
}  // namespace robot::comm
