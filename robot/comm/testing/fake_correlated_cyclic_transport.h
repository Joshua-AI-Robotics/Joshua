#pragma once

#include <cstdint>
#include <deque>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "robot/comm/interfaces/correlated_cyclic_transport.h"

namespace robot::comm {

// In-memory CorrelatedCyclicTransport. Exchange pops queued responses in FIFO
// order and reports DEADLINE_EXCEEDED once the queue is empty.
class FakeCorrelatedCyclicTransport : public CorrelatedCyclicTransport {
 public:
  absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request,
                                                absl::Duration timeout) override {
    exchange_calls_++;
    last_request_.assign(request.begin(), request.end());
    requests_.push_back(last_request_);
    last_timeout_ = timeout;
    if (!exchange_status_.ok()) {
      return exchange_status_;
    }
    if (queued_responses_.empty()) {
      return absl::DeadlineExceededError("FakeCorrelatedCyclicTransport has no queued response.");
    }
    auto response = std::move(queued_responses_.front());
    queued_responses_.pop_front();
    return response;
  }

  void QueueResponse(std::vector<uint8_t> response) {
    queued_responses_.push_back(std::move(response));
  }

  int exchange_calls_ = 0;
  absl::Status exchange_status_ = absl::OkStatus();
  absl::Duration last_timeout_ = absl::ZeroDuration();
  std::vector<uint8_t> last_request_;
  std::vector<std::vector<uint8_t>> requests_;
  std::deque<std::vector<uint8_t>> queued_responses_;
};

}  // namespace robot::comm
