#pragma once

#include <cstddef>
#include <cstdint>

#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace robot::comm {

// Finds where a response ends on a link without message boundaries, such as
// serial. The consuming protocol supplies it; links with native boundaries
// ignore it.
class MessageFramer {
 public:
  virtual ~MessageFramer() = default;

  // Returns how many more bytes complete the message that begins with
  // `received`, or 0 once it is complete. Called first with no bytes. An error
  // means `received` cannot begin a valid message.
  virtual absl::StatusOr<size_t> RemainingBytes(absl::Span<const uint8_t> received) const = 0;
};

}  // namespace robot::comm
