// Caller-sized serial transactions for vendor and JoshuaWire v1 consumers.
#pragma once

#include <memory>
#include <utility>

#include "robot/comm/interfaces/legacy_message_transport.h"
#include "robot/comm/serial/serial.h"

namespace robot::comm {

class CallerSizedSerialTransport final : public LegacyMessageTransport {
 public:
  explicit CallerSizedSerialTransport(std::shared_ptr<Serial> serial)
      : serial_(std::move(serial)) {}

  absl::Status Open() override {
    return serial_->Open();
  }
  absl::Status Write(const std::vector<uint8_t>& request) override {
    return serial_->Write(request);
  }
  absl::StatusOr<std::vector<uint8_t>> SendAndReceive(const std::vector<uint8_t>& request,
                                                      size_t expected_response_size) override {
    return serial_->AtomicRead(request, expected_response_size);
  }

 private:
  std::shared_ptr<Serial> serial_;
};

}  // namespace robot::comm
