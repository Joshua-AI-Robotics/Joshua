// Comm-internal owner loop for the future JW2 EtherCAT adapters. Clients publish
// complete output shadows and wait for input snapshots; only the worker uses I/O.
// Not factory-wired yet: the existing synchronous TI-demo path remains separate.
#pragma once

#include <chrono>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "robot/comm/ethercat/ethercat_transport.h"

namespace robot::comm::ethercat {

struct SdoAddress {
  uint16_t slave;
  uint16_t index;
  uint8_t subindex;
};

// Production backend seam, also implemented by hardware-free tests. Ownership
// transfers to EthercatMaster; no other caller may retain/use the backend.
class EthercatMasterIo : public EthercatTransport {
 public:
  using EthercatTransport::ExchangeProcessData;
  virtual absl::StatusOr<ProcessData> ExchangeProcessData(int timeout_us) = 0;
  virtual absl::Status CheckOperational(int timeout_us) = 0;
  // Initialization-only. SOEM's SDO timeout is a per-wait allowance, NOT a
  // whole-operation deadline; segmented/internal waits may exceed it.
  virtual absl::StatusOr<std::vector<uint8_t>> ReadSdo(SdoAddress address,
                                                       size_t capacity,
                                                       int timeout_us) = 0;
  virtual absl::Status WriteSdo(SdoAddress address,
                                const std::vector<uint8_t>& bytes,
                                int timeout_us) = 0;
};

class EthercatMaster {
 public:
  using Bytes = std::vector<uint8_t>;
  using Microseconds = std::chrono::microseconds;
  struct Options {
    // Deliberately no runtime defaults. Future factory assembly must obtain
    // timing policy from protobuf config, not board-type constants.
    Microseconds period;
    Microseconds process_timeout;
    Microseconds state_timeout;
    Microseconds operation_timeout;
  };
  struct Snapshot {
    uint64_t sequence = 0;
    ProcessData data;
  };

  static absl::StatusOr<std::unique_ptr<EthercatMaster>> Open(std::unique_ptr<EthercatMasterIo> io,
                                                              const std::string& interface_name,
                                                              ProcessDataMode mode,
                                                              Options options);
  ~EthercatMaster();

  const std::vector<SlaveIdentity>& slaves() const {
    return slaves_;
  }
  const std::vector<PdoRegion>& regions() const {
    return regions_;
  }
  // One protocol-validated invalid/disabled image per discovered slave, in
  // slave order. Used before OP and on shutdown; zero is NOT universally safe.
  absl::Status StartCyclic(std::vector<Bytes> stop_images);
  absl::Status SetOutputs(uint16_t slave, Bytes bytes);
  absl::StatusOr<Snapshot> WaitForCycle(uint64_t after_sequence, Microseconds timeout);
  absl::StatusOr<Bytes> ReadSdo(SdoAddress address, size_t capacity, Microseconds timeout);
  absl::Status WriteSdo(SdoAddress address, Bytes bytes, Microseconds timeout);

  // Immediately rejects work/wakes waiters, then joins. An already-dispatched
  // SOEM startup/SDO call cannot be interrupted; join has NO hard time bound.
  // Output delivery is best-effort, not a substitute for a firmware watchdog.
  absl::Status Stop();

 private:
  using Clock = std::chrono::steady_clock;
  struct Work;
  EthercatMaster(std::unique_ptr<EthercatMasterIo> io, Options options);
  absl::StatusOr<Bytes> Submit(std::shared_ptr<Work> work, Microseconds timeout);
  void Run(const std::string& interface_name, ProcessDataMode mode);
  absl::StatusOr<ProcessData> Exchange(const std::vector<Bytes>& outputs);
  void FailLocked(absl::Status status);
  void FinishLocked(const std::shared_ptr<Work>& work, absl::StatusOr<Bytes> result);

  const Options options_;
  std::unique_ptr<EthercatMasterIo> io_;
  // Published before Open returns and immutable afterward.
  std::vector<SlaveIdentity> slaves_;
  std::vector<PdoRegion> regions_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool ready_ = false;
  bool stopping_ = false;
  bool cyclic_ = false;
  absl::Status terminal_status_;
  absl::Status shutdown_status_;
  std::deque<std::shared_ptr<Work>> queue_;
  std::shared_ptr<Work> active_;
  std::vector<Bytes> outputs_;
  Snapshot snapshot_;
  // Caller-only join serialization. Worker never takes this lock, and never
  // holds mutex_ during I/O or caller notification. There are no adapter locks.
  std::mutex join_mutex_;
  std::thread worker_;
};

}  // namespace robot::comm::ethercat
