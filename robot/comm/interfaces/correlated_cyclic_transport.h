#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace robot::comm {

// Request/response exchange carried in correlated slots of a cyclic process
// image. One request is in flight per adapter; concurrent callers wait for the
// slot. The adapter carries complete protocol frames and does not decide which
// commands may use it.
class CorrelatedCyclicTransport {
 public:
  virtual ~CorrelatedCyclicTransport() = default;

  // Returns the response correlated with `request`, or DEADLINE_EXCEEDED if
  // none is published within `timeout`.
  virtual absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request,
                                                        absl::Duration timeout) = 0;
};

}  // namespace robot::comm
