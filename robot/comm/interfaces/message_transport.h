#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"

namespace robot::comm {

// Acyclic exchange of complete messages. Implementations serialize calls so
// two exchanges on a shared link never interleave.
class MessageTransport {
 public:
  virtual ~MessageTransport() = default;

  // Delivers one complete request for which no response is expected.
  virtual absl::Status Send(absl::Span<const uint8_t> request, absl::Duration timeout) = 0;

  // Delivers one complete request and returns the complete response to it,
  // or DEADLINE_EXCEEDED if none arrives within `timeout`.
  virtual absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request,
                                                        absl::Duration timeout) = 0;
};

}  // namespace robot::comm
