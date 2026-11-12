#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "robot/board/pin/pin_source.h"
#include "robot/board/proto/board.pb.h"

namespace robot::board {

// A configured claim: physical identity and intended use travel together.
struct PinResource {
  std::string profile_id;
  uint32_t profile_version;
  std::string connector;
  PinInfo info;
  DigitalPinConfig config;
  std::string identity() const;
};

absl::StatusOr<const PinInfo*> ResolvePinReference(const PinProfile& profile, const PinRef& ref);
// Idempotent, hardware-free normalization to the existing firmware numeric IDs.
absl::StatusOr<Board> ResolveMcuBoardPins(const Board& config);

absl::StatusOr<PinResource> ResolvePin(const PinProfile& profile, const DigitalPinConfig& config);
// Pure validation/resolution: does not inspect /dev or acquire any resource.
absl::StatusOr<std::vector<PinResource>> ResolveHostGpioPins(const Board& config);

}  // namespace robot::board
