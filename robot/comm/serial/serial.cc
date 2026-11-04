#include "robot/comm/serial/serial.h"

#include <climits>
#include <cmath>

namespace robot::comm {
namespace {
using Ms = std::chrono::milliseconds;
}

absl::StatusOr<std::shared_ptr<Serial>> Serial::Create(const std::string& port,
                                                       int baudrate,
                                                       Ms settle,
                                                       SerialIoOptions options) {
  auto owner = SerialIoOwner::Open(port, baudrate, settle, options);
  if (!owner.ok()) return owner.status();
  return std::shared_ptr<Serial>(new Serial(std::move(*owner)));
}

absl::Status Serial::Open() {
  return owner_->status();
}
absl::Status Serial::Write(const std::vector<uint8_t>& data) {
  if (data.size() > owner_->options().max_transfer_bytes)
    return absl::ResourceExhaustedError("Serial write exceeds configured transfer limit");
  SerialIoOwner::Request request;
  request.bytes = data;
  request.timeout = Ms(owner_->options().write_timeout_ms);
  return owner_->Submit(std::move(request)).Wait().status;
}
absl::StatusOr<std::vector<uint8_t>> Serial::Read(size_t bytes_to_read) {
  SerialIoOwner::Request request;
  request.response_bytes = bytes_to_read;
  request.timeout = Ms(owner_->options().read_timeout_ms);
  request.read_idle_timeout = Ms(owner_->options().read_idle_timeout_ms);
  auto result = owner_->Submit(std::move(request)).Wait();
  if (!result.status.ok()) return result.status;
  return std::move(result.response);
}
absl::StatusOr<std::vector<uint8_t>> Serial::AtomicRead(const std::vector<uint8_t>& command,
                                                        size_t expected_response_size) {
  if (command.size() > owner_->options().max_transfer_bytes ||
      expected_response_size > owner_->options().max_transfer_bytes - command.size())
    return absl::ResourceExhaustedError("Serial transaction exceeds configured transfer limit");
  SerialIoOwner::Request request;
  request.bytes = command;
  request.response_bytes = expected_response_size;
  request.timeout = Ms(owner_->options().transaction_timeout_ms);
  request.read_idle_timeout = Ms(owner_->options().read_idle_timeout_ms);
  request.input = InputFlushPolicy::kDiscard;
  auto result = owner_->Submit(std::move(request)).Wait();
  if (!result.status.ok()) return result.status;
  return std::move(result.response);
}
absl::Status Serial::Flush() {
  SerialIoOwner::Request request;
  request.timeout = Ms(owner_->options().write_timeout_ms);
  request.input = InputFlushPolicy::kDiscard;
  return owner_->Submit(std::move(request)).Wait().status;
}
absl::Status Serial::ExchangeUntil(absl::Span<const uint8_t> bytes,
                                   double timeout,
                                   InputFlushPolicy input,
                                   const ResponseBoundary& consume) {
  if (!std::isfinite(timeout) || timeout <= 0 || timeout > INT_MAX / 1000.0)
    return absl::InvalidArgumentError("Invalid serial transaction deadline");
  if (bytes.empty() || !consume) return absl::InvalidArgumentError("Empty serial exchange/framer");
  if (bytes.size() >= owner_->options().max_transfer_bytes)
    return absl::ResourceExhaustedError("Serial exchange exceeds configured transfer limit");
  SerialIoOwner::Request request;
  request.bytes.assign(bytes.begin(), bytes.end());
  request.response_bytes = owner_->options().max_transfer_bytes - bytes.size();
  request.timeout = std::chrono::ceil<Ms>(std::chrono::duration<double>(timeout));
  request.input = input;
  request.consume = consume;
  return owner_->Submit(std::move(request)).Wait().status;
}
}  // namespace robot::comm
