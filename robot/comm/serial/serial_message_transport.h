#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "robot/comm/interfaces/message_framer.h"
#include "robot/comm/interfaces/message_transport.h"
#include "robot/comm/serial/serial.h"

namespace robot::comm {

// MessageTransport over a shared serial port. Responses are delimited by the
// consuming protocol's framer, since serial has no message boundaries.
class SerialMessageTransport : public MessageTransport {
 public:
  SerialMessageTransport(std::shared_ptr<Serial> serial,
                         std::shared_ptr<const MessageFramer> framer)
      : serial_(std::move(serial)), framer_(std::move(framer)) {}

  absl::Status Send(absl::Span<const uint8_t> request, absl::Duration timeout) override {
    return serial_->Send(request, timeout);
  }

  absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request,
                                                absl::Duration timeout) override {
    return serial_->Exchange(request, *framer_, timeout);
  }

 private:
  std::shared_ptr<Serial> serial_;
  std::shared_ptr<const MessageFramer> framer_;
};

}  // namespace robot::comm
