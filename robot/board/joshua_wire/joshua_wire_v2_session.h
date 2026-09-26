// Declares the host-side v2 session adapter used beneath JoshuaWireBoard during
// migration. Owns session/message IDs, serialization and saved channel configs;
// the firmware-side counterpart is firmware/common/joshua_wire_v2_firmware_session.h.
// Existing command helpers are reused in memory, but transport frames are v2.
// TODO(engine composition): Remove the v1-shaped wrapper interface when the
// board engine uses neutral payload helpers and MessageTransport::Exchange
// directly; retain session/correlation safety. See firmware/common/README.md.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "robot/comm/interfaces/message_transport.h"

namespace robot::board {

// Migration adapter below the existing v1 command-payload engine. Its public
// fixed-size calls carry v1 encoded payloads in memory; only v2 frames reach the
// supplied message transport. IDs, reset and response validation are protocol
// behavior and stay in board/. Comm owns framing, I/O deadlines and bus locks.
class JoshuaWireV2Session : public robot::comm::MessageTransport {
 public:
  using SessionIdSource = std::function<uint32_t()>;
  explicit JoshuaWireV2Session(std::shared_ptr<robot::comm::MessageTransport> transport,
                               SessionIdSource source = {},
                               uint32_t message_id_limit = UINT32_MAX);

  // Every open starts a fresh session. Outputs stay disabled after resets.
  absl::Status Open() override;
  // Serial calls have a bounded I/O deadline. Close waits for that exchange,
  // sends ESTOP if the session is usable, then rejects retained channel calls.
  absl::Status Close();
  absl::Status Write(const std::vector<uint8_t>& request) override;
  absl::StatusOr<std::vector<uint8_t>> SendAndReceive(const std::vector<uint8_t>& request,
                                                      size_t expected_response_size) override;

 private:
  absl::Status ResetLocked();
  absl::Status RotateLocked();
  absl::StatusOr<std::vector<uint8_t>> ExchangeLocked(uint8_t cmd,
                                                      uint8_t channel,
                                                      const uint8_t* payload,
                                                      uint8_t payload_len);
  std::shared_ptr<robot::comm::MessageTransport> transport_;
  SessionIdSource source_;
  const uint32_t message_id_limit_;
  std::mutex mutex_;
  uint32_t session_id_ = 0;
  uint32_t next_message_id_ = 1;
  bool ready_ = false;
  std::map<uint8_t, std::vector<uint8_t>> channel_configs_;
};

}  // namespace robot::board
