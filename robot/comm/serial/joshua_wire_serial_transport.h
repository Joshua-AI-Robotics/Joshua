// JoshuaWire sync/length framing. Serial owns the physical port and transaction
// deadline; board sessions own CRC, version and correlation.
#pragma once

#include <memory>
#include <utility>

#include "robot/comm/interfaces/message_transport.h"
#include "robot/comm/serial/serial.h"

namespace robot::comm {

class JoshuaWireSerialTransport final : public MessageTransport {
 public:
  explicit JoshuaWireSerialTransport(std::shared_ptr<Serial> serial, double timeout)
      : serial_(std::move(serial)), timeout_(timeout) {}

  absl::Status Send(absl::Span<const uint8_t> request) override {
    return serial_->Write(std::vector<uint8_t>(request.begin(), request.end()));
  }
  absl::StatusOr<std::vector<uint8_t>> Exchange(absl::Span<const uint8_t> request) override;

 private:
  std::shared_ptr<Serial> serial_;
  double timeout_;
};

}  // namespace robot::comm
