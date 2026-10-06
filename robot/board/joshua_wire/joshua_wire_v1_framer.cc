#include "robot/board/joshua_wire/joshua_wire_v1_framer.h"

#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "firmware/common/joshua_wire_v1.h"

namespace robot::board {

namespace {

// sync + len.
constexpr size_t kHeaderBytes = 2;
// `len` covers proto_ver, cmd, channel, and the payload.
constexpr size_t kMinLen = 3;
constexpr size_t kMaxLen = 3 + JW1_MAX_PAYLOAD_LEN;

}  // namespace

absl::StatusOr<size_t> JoshuaWireV1Framer::RemainingBytes(
    absl::Span<const uint8_t> received) const {
  if (!received.empty() && received[0] != JW1_SYNC_BYTE) {
    return absl::DataLossError(absl::StrCat("joshua_wire_v1 response starts with 0x",
                                            absl::Hex(received[0], absl::kZeroPad2),
                                            " instead of the sync byte."));
  }
  if (received.size() < kHeaderBytes) {
    return kHeaderBytes - received.size();
  }
  const size_t len = received[1];
  if (len < kMinLen || len > kMaxLen) {
    return absl::DataLossError(
        absl::StrCat("joshua_wire_v1 response declares invalid length ", len, "."));
  }
  const size_t frame_bytes = len + 4;
  if (received.size() > frame_bytes) {
    return absl::InternalError("joshua_wire_v1 framer received more bytes than one frame.");
  }
  return frame_bytes - received.size();
}

}  // namespace robot::board
