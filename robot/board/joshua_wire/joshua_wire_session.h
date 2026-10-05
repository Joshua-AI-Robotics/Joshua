// Host-side v2 session lifecycle, IDs, serialization and saved channel configs.
// Callers supply neutral commands and receive payloads, not encoded v1 frames.
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
  // Transport calls have a bounded I/O deadline. Close waits for that exchange,
  // sends ESTOP if the session is usable, then rejects retained channel calls.
  absl::Status Close();
  // The command payload is borrowed for the call; the returned payload is owned.
  // RESET_SESSION is reserved for Open/rotation, not callable as a normal command.
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
  std::map<uint8_t, std::vector<uint8_t>> channel_configs_;
};

}  // namespace robot::board
