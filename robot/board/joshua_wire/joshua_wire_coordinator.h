// One queued protocol/session owner for all handles of a duplex MCU connection.
#pragma once
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "firmware/common/joshua_wire_commands.h"
#include "firmware/common/joshua_wire_sensor_stream.h"
#include "robot/board/joshua_wire/coordinator_policy.h"
#include "robot/comm/interfaces/frame_stream_transport.h"

namespace robot::board {
class JoshuaWireCoordinator : public std::enable_shared_from_this<JoshuaWireCoordinator> {
  struct Impl;

 public:
  using Clock = robot::comm::FrameStreamTransport::Clock;
  struct Result {
    absl::Status status;
    std::vector<uint8_t> payload;
    bool published = false;
    uint32_t session_id = 0;
    uint32_t message_id = 0;
  };
  struct Diagnostics {
    bool ready = false;
    bool closed = false;
    bool uncertain = false;
    bool faulted = false;
    bool safety_interlock = false;
    uint32_t session_id = 0;
    size_t pending_operations = 0;
    size_t pending_bytes = 0;
    bool stop_pending = false;
    uint64_t admitted = 0, completed = 0, rejected = 0, timed_out = 0, cancelled = 0;
    uint64_t retries = 0, resets = 0, retired = 0, stale_replies = 0;
    uint64_t notifications = 0, notifications_dropped = 0, stale_notifications = 0;
    uint64_t invalid_samples = 0, sequence_gaps = 0, boot_discontinuities = 0;
    uint64_t heartbeats = 0, heartbeat_failures = 0;
    size_t active_streams = 0, retired_streams = 0;
    std::string last_error;
  };
  struct Observation {
    jw_stream_sample_t sample{};
    jw_sensor_schema_t schema{};
    Clock::time_point received_at;
    std::chrono::microseconds receipt_age{0};
    uint64_t sequence_gaps = 0;
    uint64_t overwritten = 0;
    // Local receipt freshness only. Device acquisition time remains in its
    // advertised clock; no cross-clock mapping or absolute age is invented.
    bool receipt_fresh = false;
    bool usable = false;
  };
  class Subscription {
   public:
    ~Subscription();
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    absl::StatusOr<Observation> Latest() const;
    absl::Status Close();

   private:
    struct State;
    Subscription(std::shared_ptr<JoshuaWireCoordinator> owner, std::shared_ptr<State> state)
        : owner_(std::move(owner)), state_(std::move(state)) {}
    std::shared_ptr<JoshuaWireCoordinator> owner_;
    std::shared_ptr<State> state_;
    std::atomic<bool> closed_{false};
    friend class JoshuaWireCoordinator;
    friend struct Impl;
  };
  class Operation {
   public:
    void Cancel() const;
    bool Ready() const;
    Result Wait() const;

   private:
    struct State;
    explicit Operation(std::shared_ptr<State> state) : state_(std::move(state)) {}
    std::shared_ptr<State> state_;
    friend class JoshuaWireCoordinator;
    friend struct Impl;
  };
  // Repeated acquisition of the same capability shares the queue/session and
  // performs only one reset. A terminally closed/faulted lease never reopens.
  static absl::StatusOr<std::shared_ptr<JoshuaWireCoordinator>> Acquire(
      std::shared_ptr<robot::comm::FrameStreamTransport> link,
      const JoshuaWireHostPolicy& policy = {},
      std::function<uint32_t()> source = {});
  ~JoshuaWireCoordinator();
  JoshuaWireCoordinator(const JoshuaWireCoordinator&) = delete;
  JoshuaWireCoordinator& operator=(const JoshuaWireCoordinator&) = delete;
  Operation Submit(const jw_command_t& command,
                   Clock::time_point deadline = Clock::time_point::max());
  absl::StatusOr<std::vector<uint8_t>> Exchange(
      const jw_command_t& command, Clock::time_point deadline = Clock::time_point::max());
  // Diagnostic raw envelopes only, not validated sensor observations. P3c
  // owns subscription/schema/boot/freshness validation above this bounded cache.
  std::vector<robot::comm::FrameStreamTransport::ReceivedFrame> DrainNotifications();
  Diagnostics Snapshot() const;
  absl::Status Close();
  absl::StatusOr<std::shared_ptr<Subscription>> Subscribe(uint8_t function, uint32_t period_us);
  // Query does not clear uncertainty. Uncertain create/cancel requires deliberate
  // disabled reset/reconnect rather than allocating a replacement token.
  absl::StatusOr<jw_stream_state_t> QuerySubscription(uint8_t function);
  using Reopen =
      std::function<absl::StatusOr<std::shared_ptr<robot::comm::FrameStreamTransport>>()>;
  using Reconcile = std::function<absl::Status(JoshuaWireCoordinator&)>;
  // Caller chooses the intended endpoint and validates its identity/catalog.
  // Reconciliation runs with mutations interlocked. No targets/subscriptions or
  // enable state are replayed. Retained old handles remain invalid forever.
  absl::StatusOr<std::shared_ptr<JoshuaWireCoordinator>> Reconnect(
      Reopen reopen, Reconcile reconcile, std::function<uint32_t()> source = {});

 private:
  explicit JoshuaWireCoordinator(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
  std::shared_ptr<Impl> impl_;
};
}  // namespace robot::board
