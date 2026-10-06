// Temporary fixed-length compatibility seam for vendor serial consumers.
// New adapters implement MessageTransport directly. Remove this seam once those
// consumers have protocol-aware framed adapters and use Send/Exchange only.
#pragma once

#include <cstddef>
#include <memory>

#include "robot/comm/interfaces/message_transport.h"

namespace robot::comm {

class LegacyMessageTransport : public MessageTransport {
 public:
  virtual absl::Status Open() = 0;
  virtual absl::Status Write(const std::vector<uint8_t>& request) = 0;
  virtual absl::StatusOr<std::vector<uint8_t>> SendAndReceive(const std::vector<uint8_t>& request,
                                                              size_t expected_response_size) = 0;

  absl::Status Send(absl::Span<const uint8_t> request) override {
    return Write(std::vector<uint8_t>(request.begin(), request.end()));
  }
  // Never guess a response size or silently reinterpret another protocol.
  absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t>) override {
    return absl::UnimplementedError("Legacy transport has no framed Exchange adapter.");
  }
};

inline absl::StatusOr<std::shared_ptr<LegacyMessageTransport>> GetLegacyMessageTransport(
    const std::shared_ptr<MessageTransport>& transport) {
  auto legacy = std::dynamic_pointer_cast<LegacyMessageTransport>(transport);
  if (!legacy)
    return absl::FailedPreconditionError("This protocol still requires fixed-length exchanges.");
  return legacy;
}
}  // namespace robot::comm
