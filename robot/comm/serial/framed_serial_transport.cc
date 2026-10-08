// JoshuaWire sync/length framing only; deliberately does not interpret commands
// or retry responses with invalid CRC/session/message IDs.
#include "robot/comm/serial/framed_serial_transport.h"

#include "firmware/common/joshua_wire.h"

namespace robot::comm {

absl::StatusOr<std::vector<uint8_t>> FramedSerialTransport::Exchange(
    absl::Span<const uint8_t> request) {
  if (request.size() < JW_MIN_FRAME_LEN || request.size() > JW_MAX_FRAME_LEN ||
      request.front() != JW_SYNC_BYTE ||
      static_cast<size_t>(request[JW_LENGTH_OFFSET]) + JW_LENGTH_FIELD_OVERHEAD != request.size()) {
    return absl::InvalidArgumentError("Serial Exchange requires a bounded JoshuaWire frame.");
  }
  std::vector<uint8_t> response;
  size_t total = 0;
  auto status =
      serial_->ExchangeUntil(request, timeout_, [&](uint8_t byte) -> absl::StatusOr<bool> {
        if (response.empty() && byte != JW_SYNC_BYTE) return false;
        response.push_back(byte);
        if (response.size() == JW_LENGTH_PREFIX_LEN) {
          total = static_cast<size_t>(byte) + JW_LENGTH_FIELD_OVERHEAD;
          if (total < JW_MIN_FRAME_LEN || total > JW_MAX_FRAME_LEN)
            return absl::DataLossError("Serial response frame length is invalid; outcome unknown.");
        }
        return total != 0 && response.size() == total;
      });
  if (!status.ok()) return status;
  return response;
}

}  // namespace robot::comm
