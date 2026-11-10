// Comm-internal Linux serial owner. One worker performs every post-open I/O.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/interfaces/serial_diagnostics.h"
#include "robot/comm/serial/serial_io_options.h"

namespace robot::comm {
class SerialIoOwner {
 private:
  struct Impl;

 public:
  enum class InputFlushPolicy { kPreserve, kDiscard };
  enum class Publication { kNotSent, kMayHaveReachedDevice };
  using ResponseBoundary = std::function<absl::StatusOr<bool>(uint8_t)>;
  struct Receiver {
    // Comm framing only, bounded/nonblocking, no owner reentry/destruction.
    // Storage is borrowed for the call. Failure faults the physical owner.
    std::function<absl::Status(const uint8_t*, size_t)> bytes;
    std::function<void(absl::Status)> stopped;
    size_t max_write_bytes = 0;
  };
  struct Request {
    std::vector<uint8_t> bytes;
    // Exact response length, or byte limit when consume is set. Partial bytes
    // remain in Result on failure; legacy Read() facades discard them on error.
    size_t response_bytes = 0;
    std::chrono::milliseconds timeout{0};
    // Optional upper bound from a caller's complete operation budget. Never
    // renew it when copying/admitting an already prepared frame.
    std::optional<std::chrono::steady_clock::time_point> deadline;
    std::chrono::milliseconds read_idle_timeout{0};  // Zero disables idle limit.
    InputFlushPolicy input = InputFlushPolicy::kPreserve;
    // Comm framing only: bounded/nonblocking, owned captures, no owner reentry
    // or destruction. Application callbacks belong outside the I/O worker.
    ResponseBoundary consume;
  };
  struct Result {
    absl::Status status;
    Publication publication = Publication::kNotSent;
    size_t bytes_written = 0;
    size_t bytes_read = 0;
    std::vector<uint8_t> response;
  };
  class Operation {
   public:
    void Cancel() const;
    bool Ready() const;
    // An optional caller-side cancellation predicate never runs on the I/O worker.
    Result Wait(const std::function<bool()>& cancelled = {}) const;

   private:
    struct State;
    explicit Operation(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
    friend class SerialIoOwner;
    friend struct Impl;
  };
  struct PortIdentity {
    std::string canonical_path;
    std::string key;  // Filesystem/device identity; aliases share the same key.
  };

  static absl::StatusOr<PortIdentity> Identify(const std::string& path);
  static absl::StatusOr<std::shared_ptr<SerialIoOwner>> Open(
      const std::string& path,
      int baudrate,
      std::chrono::milliseconds post_open_settle = std::chrono::milliseconds(0),
      SerialIoOptions options = {},
      std::optional<Receiver> receiver = std::nullopt);
  ~SerialIoOwner();
  SerialIoOwner(const SerialIoOwner&) = delete;
  SerialIoOwner& operator=(const SerialIoOwner&) = delete;
  Operation Submit(Request request);
  absl::Status status() const;
  SerialDiagnostics Snapshot() const;
  // Terminal close. Existing handles stay closed; reopening creates a new
  // generation after old capabilities are released. Never called on the owner.
  absl::Status Close();
  const SerialIoOptions& options() const;
  const PortIdentity& identity() const;

 private:
  explicit SerialIoOwner(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
  std::shared_ptr<Impl> impl_;
};
}  // namespace robot::comm
