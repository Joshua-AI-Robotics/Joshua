// Hardware-free validation shared by config preflight and the serial factory.
#pragma once

#include <climits>

#include "absl/status/status.h"
#include "robot/comm/proto/comm.pb.h"
#include "robot/comm/serial/serial_io_options.h"

namespace robot::comm {

inline constexpr uint32_t kDefaultSerialExchangeTimeoutMs = 100;

inline SerialIoOptions SerialOptions(const SerialConfig& config) {
  SerialIoOptions options;
  const auto& p = config.io_policy();
  if (p.has_read_timeout_ms()) options.read_timeout_ms = p.read_timeout_ms();
  if (p.has_read_idle_timeout_ms()) options.read_idle_timeout_ms = p.read_idle_timeout_ms();
  if (p.has_write_timeout_ms()) options.write_timeout_ms = p.write_timeout_ms();
  if (p.has_transaction_timeout_ms()) options.transaction_timeout_ms = p.transaction_timeout_ms();
  if (p.has_max_pending_operations()) options.max_pending_operations = p.max_pending_operations();
  if (p.has_max_pending_bytes()) options.max_pending_bytes = p.max_pending_bytes();
  if (p.has_max_transfer_bytes()) options.max_transfer_bytes = p.max_transfer_bytes();
  return options;
}

inline absl::Status ValidateSerialOptions(const SerialIoOptions& p) {
  for (auto timeout :
       {p.read_timeout_ms, p.read_idle_timeout_ms, p.write_timeout_ms, p.transaction_timeout_ms}) {
    if (timeout == 0 || timeout > INT_MAX)
      return absl::InvalidArgumentError("Serial I/O timeouts must be 1..INT_MAX milliseconds");
  }
  if (p.max_pending_operations == 0 || p.max_pending_operations > 1024 ||
      p.max_pending_bytes == 0 || p.max_pending_bytes > 16 * 1024 * 1024 ||
      p.max_transfer_bytes == 0 || p.max_transfer_bytes > 1024 * 1024 ||
      p.max_transfer_bytes > p.max_pending_bytes)
    return absl::InvalidArgumentError("Serial I/O queue/transfer limits are invalid");
  return absl::OkStatus();
}

inline absl::Status ValidateSerialCommConfig(const Comm& comm) {
  if (comm.comm_type() != SERIAL || !comm.has_serial_config()) {
    return absl::InvalidArgumentError("SERIAL comm requires serial_config.");
  }
  const auto& config = comm.serial_config();
  const auto policy_status = ValidateSerialOptions(SerialOptions(config));
  if (!policy_status.ok()) return policy_status;
  if (config.port().empty() || config.baudrate() == 0) {
    return absl::InvalidArgumentError("Serial comm requires a port and baudrate.");
  }
  if (comm.transport_type() == BYTE_STREAM) {
    if (config.message_adapter() != SERIAL_MESSAGE_ADAPTER_UNSPECIFIED ||
        config.has_exchange_timeout_ms()) {
      return absl::InvalidArgumentError("BYTE_STREAM serial comm cannot select message settings.");
    }
    return absl::OkStatus();
  }
  if (comm.transport_type() != MESSAGE) {
    return absl::InvalidArgumentError("SERIAL requires BYTE_STREAM or MESSAGE transport_type.");
  }
  switch (config.message_adapter()) {
    case CALLER_SIZED:
      if (config.has_exchange_timeout_ms()) {
        return absl::InvalidArgumentError("exchange_timeout_ms requires JOSHUA_WIRE.");
      }
      return absl::OkStatus();
    case JOSHUA_WIRE:
      if (config.has_exchange_timeout_ms() &&
          (config.exchange_timeout_ms() == 0 || config.exchange_timeout_ms() > INT_MAX)) {
        return absl::InvalidArgumentError("JoshuaWire serial exchange timeout is out of range.");
      }
      return absl::OkStatus();
    case SERIAL_MESSAGE_ADAPTER_UNSPECIFIED:
    default:
      return absl::InvalidArgumentError(
          "Serial MESSAGE requires message_adapter: CALLER_SIZED or JOSHUA_WIRE.");
  }
}

}  // namespace robot::comm
