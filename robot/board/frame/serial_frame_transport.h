#pragma once

#include <memory>

#include "robot/board/frame/frame_transport.h"
#include "robot/comm/serial/serial.h"

namespace robot::board {

// Production FrameTransport: joshua_wire_v1 frames carried over a
// robot::comm::SerialTransport (docs/BOARD_LAYER_RFC.md §7.3 — "FrameTransport
// seam on Serial"). Depends on the SerialTransport interface, not the
// concrete Serial class, so it composes with the same fakes FeetechBusBoard
// uses (robot/comm/serial/fake_serial_transport.h) — see
// serial_frame_transport_test.cc.
class SerialFrameTransport : public FrameTransport {
 public:
  explicit SerialFrameTransport(std::shared_ptr<robot::comm::SerialTransport> transport);

  absl::StatusOr<std::vector<uint8_t>> SendAndReceive(const std::vector<uint8_t>& request_frame,
                                                      size_t expected_response_len) override;

 private:
  std::shared_ptr<robot::comm::SerialTransport> transport_;
};

}  // namespace robot::board
