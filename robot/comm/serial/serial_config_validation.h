// Hardware-free validation shared by config preflight and the serial factory.
#pragma once

#include <climits>

#include "absl/status/status.h"
#include "robot/comm/proto/comm.pb.h"

namespace robot::comm {

inline constexpr uint32_t kDefaultSerialExchangeTimeoutMs = 100;

inline absl::Status ValidateSerialCommConfig(const Comm& comm) {
  if (comm.comm_type() != SERIAL || !comm.has_serial_config()) {
    return absl::InvalidArgumentError("SERIAL comm requires serial_config.");
  }
  const auto& config = comm.serial_config();
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
