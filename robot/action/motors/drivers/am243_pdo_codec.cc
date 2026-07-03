#include "robot/action/motors/drivers/am243_pdo_codec.h"

#include "absl/status/status.h"

namespace robot::action::am243 {

std::vector<uint8_t> EncodeDemoOutputSeed(uint8_t seed) {
  std::vector<uint8_t> output_pdo(kDemoPdoSizeBytes, 0);
  output_pdo[kDemoSeedByteOffset] = seed;
  return output_pdo;
}

std::vector<uint8_t> EncodeDemoOutputWalk(uint8_t seed) {
  std::vector<uint8_t> output_pdo(kDemoPdoSizeBytes, 0);
  for (int byte = 0; byte < kDemoPdoSizeBytes; ++byte) {
    output_pdo[byte] = static_cast<uint8_t>(seed + byte);
  }
  return output_pdo;
}

absl::StatusOr<uint8_t> DecodeDemoInputEchoSeed(const std::vector<uint8_t>& input_pdo) {
  if (input_pdo.size() != kDemoPdoSizeBytes) {
    return absl::Status(absl::StatusCode::kInvalidArgument,
                        "AM243 demo input PDO must be exactly 8 bytes");
  }
  return input_pdo[kDemoSeedByteOffset];
}

}  // namespace robot::action::am243
