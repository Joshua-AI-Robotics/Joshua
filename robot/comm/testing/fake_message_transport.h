#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "robot/comm/interfaces/comm_lease.h"
#include "robot/comm/interfaces/message_transport.h"

namespace robot::comm {

// In-memory MessageTransport. Exchange pops queued responses in FIFO order
// regardless of the request sent and reports DEADLINE_EXCEEDED, as a silent
// peer would, once the queue is empty.
class FakeMessageTransport : public MessageTransport {
 public:
  absl::Status Send(absl::Span<const uint8_t> request, absl::Duration timeout) override {
    send_calls_++;
    Record(request, timeout);
    return send_status_;
  }

  absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request,
                                                absl::Duration timeout) override {
    exchange_calls_++;
    Record(request, timeout);
    if (!exchange_status_.ok()) {
      return exchange_status_;
    }
    if (queued_responses_.empty()) {
      return absl::DeadlineExceededError("FakeMessageTransport has no queued response.");
    }
    auto response = std::move(queued_responses_.front());
    queued_responses_.pop_front();
    return response;
  }

  void QueueResponse(std::vector<uint8_t> response) {
    queued_responses_.push_back(std::move(response));
  }

  int send_calls_ = 0;
  int exchange_calls_ = 0;
  absl::Status send_status_ = absl::OkStatus();
  absl::Status exchange_status_ = absl::OkStatus();
  absl::Duration last_timeout_ = absl::ZeroDuration();
  std::vector<uint8_t> last_request_;
  std::vector<std::vector<uint8_t>> requests_;
  std::deque<std::vector<uint8_t>> queued_responses_;

 private:
  void Record(absl::Span<const uint8_t> request, absl::Duration timeout) {
    last_request_.assign(request.begin(), request.end());
    requests_.push_back(last_request_);
    last_timeout_ = timeout;
  }
};

// A lease whose only capability is `transport` as MESSAGE.
inline CommLease MessageOnlyLease(std::shared_ptr<MessageTransport> transport) {
  CommCapabilities capabilities;
  capabilities.message = std::move(transport);
  return CommLease(std::move(capabilities));
}

}  // namespace robot::comm
