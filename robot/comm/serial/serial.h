#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/types/span.h"
#include "robot/comm/interfaces/byte_stream.h"
#include "robot/comm/serial/serial_io_owner.h"

namespace robot::comm {

// Synchronous compatibility facade. Every operation is admitted to the same
// bounded I/O owner; callers never read or write the descriptor themselves.
class Serial : public ByteStream {
 public:
  using InputFlushPolicy = SerialIoOwner::InputFlushPolicy;
  using ResponseBoundary = SerialIoOwner::ResponseBoundary;
  static absl::StatusOr<std::shared_ptr<Serial>> Create(
      const std::string& port,
      int baudrate,
      std::chrono::milliseconds settle = std::chrono::milliseconds(0),
      SerialIoOptions options = {});

  absl::Status Write(const std::vector<uint8_t>& data) override;
  absl::StatusOr<std::vector<uint8_t>> Read(size_t bytes_to_read) override;
  absl::StatusOr<std::vector<uint8_t>> AtomicRead(const std::vector<uint8_t>& command,
                                                  size_t expected_response_size);
  absl::Status Flush();
  absl::Status Open() override;
  // Adapter framing only: bounded, nonblocking callback, no owner reentry or
  // destruction. The callback finishes before this synchronous call returns.
  // The deadline includes queue wait, write and read. Discarding input is
  // explicitly request-only behavior, unsuitable for continuous telemetry.
  absl::Status ExchangeUntil(absl::Span<const uint8_t> request,
                             std::chrono::milliseconds timeout,
                             InputFlushPolicy input_flush_policy,
                             const ResponseBoundary& consume);
  SerialDiagnostics Snapshot() const {
    return owner_->Snapshot();
  }
  const SerialIoOwner::PortIdentity& identity() const {
    return owner_->identity();
  }

 private:
  explicit Serial(std::shared_ptr<SerialIoOwner> owner) : owner_(std::move(owner)) {}
  std::shared_ptr<SerialIoOwner> owner_;
};
}  // namespace robot::comm
