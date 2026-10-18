// Fixed-length in-memory fake for remaining v1/vendor consumers. No concrete
// serial dependency or hardware I/O. Prefer fake_transports.h for new clients.
#pragma once

#include <cstddef>
#include <deque>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/interfaces/legacy_message_transport.h"

namespace robot::comm {

// In-memory legacy message double for bus-protocol boards under test.
// SendAndReceive pops queued responses in FIFO
// order regardless of the command bytes sent; queue the expected response
// before triggering the call under test.
class FakeLegacyMessageTransport : public LegacyMessageTransport {
 public:
  absl::Status Open() override {
    return absl::OkStatus();
  }
  absl::Status Write(const std::vector<uint8_t>& data) override {
    write_calls_++;
    last_written_ = data;
    written_.push_back(data);
    return write_status_;
  }

  absl::StatusOr<std::vector<uint8_t>> SendAndReceive(const std::vector<uint8_t>& command,
                                                      size_t expected_response_size) override {
    exchange_calls_++;
    last_written_ = command;
    written_.push_back(command);
    if (!exchange_status_.ok()) {
      return exchange_status_;
    }
    if (queued_responses_.empty()) {
      return std::vector<uint8_t>(expected_response_size, 0);
    }
    auto response = std::move(queued_responses_.front());
    queued_responses_.pop_front();
    return response;
  }

  void QueueResponse(std::vector<uint8_t> response) {
    queued_responses_.push_back(std::move(response));
  }

  int write_calls_ = 0;
  int exchange_calls_ = 0;
  absl::Status write_status_ = absl::OkStatus();
  absl::Status exchange_status_ = absl::OkStatus();
  std::vector<uint8_t> last_written_;
  std::vector<std::vector<uint8_t>> written_;
  std::deque<std::vector<uint8_t>> queued_responses_;
};

}  // namespace robot::comm
