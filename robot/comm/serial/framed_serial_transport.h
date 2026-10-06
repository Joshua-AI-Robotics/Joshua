// Comm-internal JoshuaWire serial message boundaries. Serial owns the physical
// port/transaction deadline; board sessions own CRC, version and correlation.
// Legacy fixed-length forwarding preserves vendor consumers.
#pragma once

#include <chrono>
#include <memory>
#include <utility>

#include "robot/comm/interfaces/legacy_message_transport.h"
#include "robot/comm/serial/serial.h"

namespace robot::comm {

class FramedSerialTransport final : public LegacyMessageTransport {
 public:
  explicit FramedSerialTransport(std::shared_ptr<Serial> serial, std::chrono::milliseconds timeout)
      : serial_(std::move(serial)), timeout_(timeout) {}

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
  absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request) override;

 private:
  std::shared_ptr<Serial> serial_;
  std::chrono::milliseconds timeout_;
};

}  // namespace robot::comm
