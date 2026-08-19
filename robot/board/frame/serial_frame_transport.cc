#include "robot/board/frame/serial_frame_transport.h"

#include <utility>

namespace robot::board {

SerialFrameTransport::SerialFrameTransport(std::shared_ptr<robot::comm::SerialTransport> transport)
    : transport_(std::move(transport)) {}

absl::StatusOr<std::vector<uint8_t>> SerialFrameTransport::SendAndReceive(
    const std::vector<uint8_t>& request_frame, size_t expected_response_len) {
  return transport_->AtomicRead(request_frame, expected_response_len);
}

}  // namespace robot::board
