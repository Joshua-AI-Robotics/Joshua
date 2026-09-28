// Acyclic message capability. Protocol consumers see complete byte messages,
// never serial ports, mailbox objects or adapter lifecycle methods.
#pragma once

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"

namespace robot::comm {

// Created ready-to-use by CommFactory. Each call borrows request storage until
// it returns; queued implementations must copy it before returning to callers.
// Implementations serialize shared-link operations and bound exchange I/O.
// A timeout after transmission does not prove that the command was not applied.
class MessageTransport {
 public:
  virtual ~MessageTransport() = default;

  // Send-only protocols do not wait for or invent an acknowledgment.
  virtual absl::Status Send(absl::Span<const uint8_t> request) = 0;
  // Adapter determines response length; returned storage belongs to the caller.
  virtual absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request) = 0;
};

}  // namespace robot::comm
