// JoshuaWire sync/length framing only; deliberately does not interpret commands
// or retry responses with invalid CRC/session/message IDs.
#include "robot/comm/serial/framed_serial_transport.h"

namespace robot::comm {

absl::StatusOr<std::vector<uint8_t>> FramedSerialTransport::Exchange(
    absl::Span<const uint8_t> request) {
  if (request.size() < 7 || request.size() > 64 || request[0] != 0xA5 ||
      static_cast<size_t>(request[1]) + 4 != request.size()) {
    return absl::InvalidArgumentError("Serial Exchange requires a bounded JoshuaWire frame.");
  }
  std::vector<uint8_t> response;
  size_t total = 0;
  auto status =
      serial_->ExchangeUntil(request, timeout_, [&](uint8_t byte) -> absl::StatusOr<bool> {
        if (response.empty() && byte != 0xA5) return false;
        response.push_back(byte);
        if (response.size() == 2) {
          total = static_cast<size_t>(byte) + 4;
          if (total < 7 || total > 64)
            return absl::DataLossError("Serial response frame length is invalid; outcome unknown.");
        }
        return total != 0 && response.size() == total;
      });
  if (!status.ok()) return status;
  return response;
}

}  // namespace robot::comm
