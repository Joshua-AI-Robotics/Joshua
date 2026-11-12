// JoshuaWire sync/length framing only; deliberately does not interpret commands
// or retry responses with invalid CRC/session/message IDs.
#include "robot/comm/serial/joshua_wire_serial_transport.h"

#include <memory>

#include "firmware/common/joshua_wire.h"

namespace robot::comm {

absl::StatusOr<std::vector<uint8_t>> JoshuaWireSerialTransport::Exchange(
    absl::Span<const uint8_t> request) {
  if (request.size() < JW_MIN_FRAME_LEN || request.size() > JW_MAX_FRAME_LEN ||
      request.front() != JW_SYNC_BYTE ||
      static_cast<size_t>(request[JW_LENGTH_OFFSET]) + JW_LENGTH_FIELD_OVERHEAD != request.size()) {
    return absl::InvalidArgumentError("Serial Exchange requires a bounded JoshuaWire frame.");
  }
  struct Frame {
    std::vector<uint8_t> response;
    size_t total = 0;
  };
  auto frame = std::make_shared<Frame>();
  frame->response.reserve(JW_MAX_FRAME_LEN);
  auto status = serial_->ExchangeUntil(
      request,
      timeout_,
      Serial::InputFlushPolicy::kDiscard,
      [frame](uint8_t byte) -> absl::StatusOr<bool> {
        auto& response = frame->response;
        if (response.empty() && byte != JW_SYNC_BYTE) return false;
        response.push_back(byte);
        if (response.size() == JW_LENGTH_PREFIX_LEN) {
          frame->total = static_cast<size_t>(byte) + JW_LENGTH_FIELD_OVERHEAD;
          if (frame->total < JW_MIN_FRAME_LEN || frame->total > JW_MAX_FRAME_LEN)
            return absl::DataLossError("Serial response frame length is invalid; outcome unknown.");
        }
        return frame->total != 0 && response.size() == frame->total;
      });
  if (!status.ok()) return status;
  return std::move(frame->response);
}

}  // namespace robot::comm
