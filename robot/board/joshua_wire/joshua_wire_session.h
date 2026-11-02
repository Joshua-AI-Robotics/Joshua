// Host-side JW session lifecycle, IDs, serialization and uncertain outcomes.
// Callers supply neutral commands and receive validated payloads.
// The firmware counterpart is firmware/common/joshua_wire_firmware_session.h.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "firmware/common/joshua_wire_commands.h"
#include "robot/comm/interfaces/correlated_cyclic_transport.h"
#include "robot/comm/interfaces/message_transport.h"

namespace robot::board {

// A board-protocol session, not a comm transport. Comm owns actual I/O,
// deadlines and bus locking; this class owns correlation and reset safety.
class JoshuaWireSession {
 public:
  using SessionIdSource = std::function<uint32_t()>;
  explicit JoshuaWireSession(std::shared_ptr<robot::comm::MessageTransport> transport,
                             SessionIdSource source = {},
                             uint32_t message_id_limit = UINT32_MAX,
                             std::shared_ptr<robot::comm::CorrelatedCyclicTransport> cyclic = {},
                             absl::Duration cyclic_timeout = absl::ZeroDuration());

  // Every open starts a fresh session. Outputs stay disabled after resets.
  absl::Status Open();
  // Reset with a fresh session to disable outputs, preserving existing ESTOPs.
  // Retained calls are rejected after any close attempt, including failures.
  absl::Status Close();
  // The command payload is borrowed for the call; the returned payload is owned.
  // RESET_SESSION is managed by the lifecycle, not callable as a normal command.
  absl::StatusOr<std::vector<uint8_t>> Exchange(const jw_command_t& command);

 private:
  absl::Status ResetLocked();
  absl::Status RotateLocked();
  absl::StatusOr<std::vector<uint8_t>> ExchangeLocked(uint8_t cmd,
                                                      uint8_t channel,
                                                      const uint8_t* payload,
                                                      uint8_t payload_len);
  std::shared_ptr<robot::comm::MessageTransport> transport_;
  std::shared_ptr<robot::comm::CorrelatedCyclicTransport> cyclic_;
  const absl::Duration cyclic_timeout_;
  SessionIdSource source_;
  const uint32_t message_id_limit_;
  std::mutex mutex_;
  uint32_t session_id_ = 0;
  uint32_t next_message_id_ = 1;
  bool ready_ = false;
  bool uncertain_ = false;
  std::map<uint8_t, std::pair<uint32_t, uint32_t>> pending_;
};

}  // namespace robot::board
