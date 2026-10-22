// Protocol-neutral message transactions whose response size comes from the caller.
#pragma once

#include <cstddef>
#include <memory>

#include "robot/comm/interfaces/message_transport.h"

namespace robot::comm {

class CallerSizedMessageTransport : public MessageTransport {
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
    return absl::UnimplementedError("Caller-sized transport requires an explicit response size.");
  }
};

inline absl::StatusOr<std::shared_ptr<CallerSizedMessageTransport>> GetCallerSizedMessageTransport(
    const std::shared_ptr<MessageTransport>& transport) {
  auto caller_sized = std::dynamic_pointer_cast<CallerSizedMessageTransport>(transport);
  if (!caller_sized)
    return absl::FailedPreconditionError("This protocol requires caller-sized exchanges.");
  return caller_sized;
}
}  // namespace robot::comm
