// Paired JW2 CoE/PDO adapters for one compatibility-gated slave. The endpoint
// worker serializes protocol requests and consumes copied master snapshots;
// only EthercatMaster's worker performs bus I/O. No motor semantics live here.
#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "robot/comm/ethercat/ethercat_master.h"
#include "robot/comm/interfaces/correlated_cyclic_transport.h"
#include "robot/comm/interfaces/message_transport.h"

namespace robot::comm::ethercat {

// Pure compatibility gate. Failure includes observed artifact/fields and the
// required profile. Does not reset sessions, enter OP or access hardware.
absl::Status ValidateJoshuaWireEthercatProfile(absl::Span<const uint8_t> descriptor,
                                               const PdoRegion& region,
                                               uint32_t required_transports);

class JoshuaWireEthercatTransport final : public MessageTransport,
                                          public CorrelatedCyclicTransport {
 public:
  using Bytes = EthercatMaster::Bytes;
  using Microseconds = EthercatMaster::Microseconds;
  struct Options {
    Microseconds exchange_timeout;
    Microseconds poll_interval;
  };
  // Call before StartCyclic, once per slave. Claims remain reserved until the
  // master is destroyed, even after failure/Stop: reopen to replace endpoints.
  // Factory assembly gates ALL slaves before starting the master.
  static absl::StatusOr<std::shared_ptr<JoshuaWireEthercatTransport>> Open(
      std::shared_ptr<EthercatMaster> master, uint16_t slave, Options options);
  ~JoshuaWireEthercatTransport() override;
  absl::Status Send(absl::Span<const uint8_t> request) override;
  absl::StatusOr<Bytes> Exchange(absl::Span<const uint8_t> request) override;
  absl::StatusOr<Bytes> Exchange(absl::Span<const uint8_t> request,
                                 absl::Duration timeout) override;
  // Wakes callers immediately and invalidates this slave's output shadow, then
  // joins the endpoint worker. Other endpoints retain their shared master.
  // Joining may wait for in-progress startup SDO; no hard shutdown bound.
  void Stop();
  static Bytes StopImage();

 private:
  using Clock = std::chrono::steady_clock;
  struct Work;
  JoshuaWireEthercatTransport(std::shared_ptr<EthercatMaster> master,
                              uint16_t slave,
                              Options options);
  absl::StatusOr<Bytes> Submit(absl::Span<const uint8_t>, bool cyclic, Microseconds);
  void Run();
  void FinishLocked(const std::shared_ptr<Work>&, absl::StatusOr<Bytes>);
  Microseconds Remaining(const std::shared_ptr<Work>&);
  absl::Status Publish(const std::shared_ptr<Work>&, Bytes);
  absl::StatusOr<Bytes> Execute(const std::shared_ptr<Work>&);
  absl::StatusOr<Bytes> Reset(const std::shared_ptr<Work>&);
  absl::StatusOr<Bytes> Mailbox(const std::shared_ptr<Work>&, uint32_t generation);
  absl::StatusOr<Bytes> Cyclic(const std::shared_ptr<Work>&, uint32_t generation);
  void DrainLatePdo();
  void Invalidate();
  bool Pause(const std::shared_ptr<Work>&);

  const std::shared_ptr<EthercatMaster> master_;
  const uint16_t slave_;
  const PdoRegion region_;
  const Options options_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stopping_ = false;
  std::deque<std::shared_ptr<Work>> queue_;
  std::shared_ptr<Work> active_;
  std::mutex join_mutex_;
  std::thread worker_;
  // Endpoint-worker-only protocol state; callers access only Work under mutex.
  uint32_t session_ = 0;
  uint32_t last_message_ = 0;
  uint32_t generation_ = 0;
  uint32_t pdo_ack_ = 0;
  uint64_t sequence_ = 0;
  bool needs_reset_ = true;
};

}  // namespace robot::comm::ethercat
