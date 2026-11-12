// Deterministic, hardware-free doubles for message and correlated-cyclic clients.
// These copy requests and return queued results; they do not simulate workers,
// elapsed deadlines, wire framing or correlation. Adapter tests must cover those.
#pragma once

#include <deque>
#include <utility>
#include <vector>

#include "robot/comm/interfaces/correlated_cyclic_transport.h"
#include "robot/comm/interfaces/message_transport.h"

namespace robot::comm::testing {

using Bytes = std::vector<uint8_t>;

class FakeMessageTransport : public MessageTransport {
 public:
  absl::Status Send(absl::Span<const uint8_t> request) override {
    sent.emplace_back(request.begin(), request.end());
    return send_status;
  }
  absl::StatusOr<Bytes> Exchange(absl::Span<const uint8_t> request) override {
    exchanged.emplace_back(request.begin(), request.end());
    if (results.empty()) return absl::FailedPreconditionError("No message result queued.");
    auto result = std::move(results.front());
    results.pop_front();
    return result;
  }

  absl::Status send_status = absl::OkStatus();
  std::vector<Bytes> sent;
  std::vector<Bytes> exchanged;
  std::deque<absl::StatusOr<Bytes>> results;
};

class FakeCorrelatedCyclicTransport : public CorrelatedCyclicTransport {
 public:
  absl::StatusOr<Bytes> Exchange(absl::Span<const uint8_t> request,
                                 absl::Duration timeout) override {
    if (timeout <= absl::ZeroDuration() || timeout == absl::InfiniteDuration())
      return absl::InvalidArgumentError("Cyclic timeout must be finite and positive.");
    exchanged.emplace_back(request.begin(), request.end());
    timeouts.push_back(timeout);
    if (results.empty()) return absl::FailedPreconditionError("No cyclic result queued.");
    auto result = std::move(results.front());
    results.pop_front();
    return result;
  }

  std::vector<Bytes> exchanged;
  std::vector<absl::Duration> timeouts;
  std::deque<absl::StatusOr<Bytes>> results;
};
}  // namespace robot::comm::testing
