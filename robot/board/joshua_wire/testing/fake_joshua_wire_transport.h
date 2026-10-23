// Test-only correlated JoshuaWire endpoint; no physical transport or I/O.
#pragma once

#include <deque>
#include <utility>
#include <vector>

#include "firmware/am243/joshua_dual_transport/src/joshua_commands.h"
#include "firmware/common/joshua_wire.h"
#include "firmware/common/joshua_wire_endpoint.h"
#include "robot/comm/interfaces/message_transport.h"

namespace robot::board {
class FakeJoshuaWireTransport : public robot::comm::MessageTransport {
 public:
  FakeJoshuaWireTransport() {
    jw_endpoint_init(&endpoint);
    JoshuaInit(&channel, 123, JW_PLANE_MESSAGE | JW_PLANE_CYCLIC);
  }
  jw_endpoint_t endpoint{};
  JoshuaChannel channel{};
  struct Response {
    uint8_t command;
    uint8_t channel;
    std::vector<uint8_t> payload;
  };
  void QueueResponse(Response response) {
    queued_.push_back(std::move(response));
  }
  absl::Status Send(absl::Span<const uint8_t>) override {
    return absl::OkStatus();
  }
  absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> bytes) override {
    ++exchange_calls_;
    last_written_.assign(bytes.begin(), bytes.end());
    written_.push_back(last_written_);
    jw_frame_t request;
    if (jw_decode_frame(bytes.data(), bytes.size(), &request) != JW_RESULT_OK)
      return absl::DataLossError("Invalid test request");
    if (queued_.empty()) {
      std::vector<uint8_t> out(JW_MAX_FRAME_LEN);
      JoshuaService(&channel, exchange_calls_);
      const int len = jw_endpoint_process(&endpoint,
                                          bytes.data(),
                                          bytes.size(),
                                          out.data(),
                                          out.size(),
                                          JoshuaCommand,
                                          JoshuaReset,
                                          &channel);
      if (len <= 0) return absl::DataLossError("Firmware discarded request");
      out.resize(len);
      return out;
    }
    Response response{request.cmd, request.channel, {JW_STATUS_OK, JW_EXECUTION_COMPLETE}};
    if (request.cmd != JW_CMD_RESET_SESSION && request.cmd != JW_CMD_ESTOP) {
      if (queued_.empty()) return absl::UnavailableError("Missing queued test response");
      response = std::move(queued_.front());
      queued_.pop_front();
    }
    std::vector<uint8_t> result(JW_MAX_FRAME_LEN);
    const int len = jw_encode_frame(result.data(),
                                    result.size(),
                                    request.session_id,
                                    request.message_id,
                                    response.command,
                                    response.channel,
                                    response.payload.data(),
                                    response.payload.size());
    if (len < 0) return absl::InternalError("Invalid queued test response");
    result.resize(len);
    return result;
  }
  int exchange_calls_ = 0;
  std::vector<uint8_t> last_written_;
  std::vector<std::vector<uint8_t>> written_;

 private:
  std::deque<Response> queued_;
};
}  // namespace robot::board
