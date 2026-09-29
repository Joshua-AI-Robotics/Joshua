// Scheduling, deadline/cancellation policy and shadow images; no SOEM types.
#include "robot/comm/ethercat/ethercat_master.h"

#include <algorithm>
#include <climits>
#include <utility>

#include "robot/comm/ethercat/ethercat_status.h"

namespace robot::comm::ethercat {
namespace {
bool ValidTimeout(EthercatMaster::Microseconds timeout) {
  return timeout.count() > 0 && timeout.count() <= INT_MAX;
}
}  // namespace

struct EthercatMaster::Work {
  enum class Kind { kStart, kRead, kWrite };
  Kind kind;
  SdoAddress address{};
  size_t capacity = 0;
  Bytes bytes;
  std::vector<Bytes> stop_images;
  Clock::time_point deadline;
  bool dispatched = false;
  bool done = false;
  absl::StatusOr<Bytes> result = absl::UnknownError("not completed");
};

EthercatMaster::EthercatMaster(std::unique_ptr<EthercatMasterIo> io, Options options)
    : options_(options), io_(std::move(io)) {}
EthercatMaster::~EthercatMaster() {
  (void)Stop();
}

absl::StatusOr<std::unique_ptr<EthercatMaster>> EthercatMaster::Open(
    std::unique_ptr<EthercatMasterIo> io,
    const std::string& interface_name,
    ProcessDataMode mode,
    Options options) {
  if (!io || interface_name.empty() || mode != ProcessDataMode::kSplitLrdLwr ||
      !ValidTimeout(options.period) || !ValidTimeout(options.process_timeout) ||
      !ValidTimeout(options.state_timeout) || !ValidTimeout(options.operation_timeout) ||
      options.mailbox_step_budget.count() < 0 || options.scheduling_guard.count() < 0 ||
      options.scheduling_guard.count() > INT_MAX ||
      (options.mailbox_step_budget.count() > 0 &&
       (!ValidTimeout(options.mailbox_step_budget) || !ValidTimeout(options.scheduling_guard))) ||
      options.period <= options.process_timeout +
                            std::max(options.state_timeout, options.mailbox_step_budget) +
                            options.scheduling_guard) {
    return absl::InvalidArgumentError("invalid EtherCAT interface/backend or timing budgets");
  }
  auto master = std::unique_ptr<EthercatMaster>(new EthercatMaster(std::move(io), options));
  master->worker_ = std::thread(&EthercatMaster::Run, master.get(), interface_name, mode);
  std::unique_lock lock(master->mutex_);
  master->cv_.wait(lock, [&] { return master->ready_; });
  const auto status = master->terminal_status_;
  lock.unlock();
  if (!status.ok()) return status;
  return master;
}

void EthercatMaster::FinishLocked(const std::shared_ptr<Work>& work, absl::StatusOr<Bytes> result) {
  if (work->done) return;
  work->result = std::move(result);
  work->done = true;
}

void EthercatMaster::FailLocked(absl::Status status) {
  if (!stopping_) terminal_status_ = std::move(status);
  stopping_ = true;
  if (active_) FinishLocked(active_, terminal_status_);
  for (const auto& work : queue_) FinishLocked(work, terminal_status_);
  queue_.clear();
}

absl::StatusOr<EthercatMaster::Bytes> EthercatMaster::Submit(std::shared_ptr<Work> work,
                                                             Microseconds timeout) {
  if (!ValidTimeout(timeout)) return absl::InvalidArgumentError("invalid operation timeout");
  work->deadline = Clock::now() + timeout;
  std::unique_lock lock(mutex_);
  if (stopping_) return terminal_status_;
  if (cyclic_ && (!runtime_sdo_ || work->kind == Work::Kind::kStart)) {
    return absl::FailedPreconditionError("cyclic already started; startup SDO access is closed");
  }
  constexpr size_t kQueueCapacity = 64;
  if (queue_.size() >= kQueueCapacity) return absl::ResourceExhaustedError("owner queue is full");
  queue_.push_back(work);
  lock.unlock();
  cv_.notify_all();
  lock.lock();
  if (!cv_.wait_until(lock, work->deadline, [&] { return work->done; })) {
    auto status = absl::DeadlineExceededError(
        work->dispatched ? "EtherCAT operation timed out after dispatch; outcome unknown"
                         : "EtherCAT operation expired before dispatch; not executed");
    FinishLocked(work, status);
    if (work->dispatched) FailLocked(status);
    lock.unlock();
    cv_.notify_all();
    return status;
  }
  return work->result;
}

absl::Status EthercatMaster::StartCyclic(std::vector<Bytes> stop_images) {
  if (stop_images.size() != regions_.size()) {
    return absl::InvalidArgumentError("a stop image is required for every EtherCAT slave");
  }
  for (size_t i = 0; i < regions_.size(); ++i) {
    if (stop_images[i].size() != regions_[i].output_size_bytes)
      return absl::InvalidArgumentError("stop image size does not match mapped PDO");
  }
  auto work = std::make_shared<Work>();
  work->kind = Work::Kind::kStart;
  work->stop_images = std::move(stop_images);
  return Submit(work, options_.operation_timeout).status();
}

absl::Status EthercatMaster::SetOutputs(uint16_t slave, Bytes bytes) {
  std::lock_guard lock(mutex_);
  if (stopping_) return terminal_status_;
  if (!cyclic_) return absl::FailedPreconditionError("cyclic exchange not started");
  if (slave == 0 || slave > regions_.size() ||
      bytes.size() != regions_[slave - 1].output_size_bytes)
    return absl::InvalidArgumentError("invalid slave or output image size");
  outputs_[slave - 1] = std::move(bytes);
  return absl::OkStatus();
}

absl::StatusOr<EthercatMaster::Snapshot> EthercatMaster::WaitForCycle(uint64_t after_sequence,
                                                                      Microseconds timeout) {
  if (!ValidTimeout(timeout)) return absl::InvalidArgumentError("invalid cycle wait timeout");
  const auto deadline = Clock::now() + timeout;
  std::unique_lock lock(mutex_);
  if (!stopping_ && !cyclic_) return absl::FailedPreconditionError("cyclic exchange not started");
  if (!cv_.wait_until(
          lock, deadline, [&] { return stopping_ || snapshot_.sequence > after_sequence; }))
    return absl::DeadlineExceededError("no new EtherCAT cycle before deadline");
  if (stopping_) return terminal_status_;
  return snapshot_;
}

absl::StatusOr<EthercatMaster::Bytes> EthercatMaster::ReadSdo(SdoAddress address,
                                                              size_t capacity,
                                                              Microseconds timeout) {
  if (address.slave == 0 || address.slave > regions_.size() || capacity == 0 || capacity > 4096)
    return absl::InvalidArgumentError("invalid SDO slave or capacity (1..4096)");
  auto work = std::make_shared<Work>();
  work->kind = Work::Kind::kRead;
  work->address = address;
  work->capacity = capacity;
  return Submit(work, timeout);
}

absl::Status EthercatMaster::WriteSdo(SdoAddress address, Bytes bytes, Microseconds timeout) {
  if (address.slave == 0 || address.slave > regions_.size() || bytes.empty() || bytes.size() > 4096)
    return absl::InvalidArgumentError("invalid SDO slave or size (1..4096)");
  auto work = std::make_shared<Work>();
  work->kind = Work::Kind::kWrite;
  work->address = address;
  work->bytes = std::move(bytes);
  return Submit(work, timeout).status();
}

absl::Status EthercatMaster::Stop() {
  {
    std::lock_guard lock(mutex_);
    FailLocked(
        absl::CancelledError("EtherCAT owner stopped; dispatched operation outcome unknown"));
  }
  cv_.notify_all();
  std::lock_guard join_lock(join_mutex_);
  if (worker_.joinable()) worker_.join();
  return shutdown_status_;
}

absl::Status EthercatMaster::ClaimEndpoint(uint16_t slave) {
  std::lock_guard lock(mutex_);
  if (stopping_) return terminal_status_;
  if (cyclic_ || active_ || !queue_.empty() || !runtime_sdo_)
    return absl::FailedPreconditionError("claim endpoints before OP on an incremental-SDO master");
  if (slave == 0 || slave > regions_.size())
    return absl::InvalidArgumentError("invalid endpoint slave");
  if (std::find(claimed_slaves_.begin(), claimed_slaves_.end(), slave) != claimed_slaves_.end())
    return absl::AlreadyExistsError("slave endpoint already claimed; reopen master to replace");
  claimed_slaves_.push_back(slave);
  return absl::OkStatus();
}

absl::Status EthercatMaster::status() {
  std::lock_guard lock(mutex_);
  return terminal_status_;
}

absl::StatusOr<ProcessData> EthercatMaster::Exchange(const std::vector<Bytes>& outputs) {
  for (size_t i = 0; i < regions_.size(); ++i) {
    auto status = io_->WriteOutputs(regions_[i], outputs[i]);
    if (!status.ok()) return status;
  }
  auto data = io_->ExchangeProcessData(static_cast<int>(options_.process_timeout.count()));
  if (!data.ok()) return data.status();
  if (data->expected_working_count <= 0)
    return absl::UnavailableError("EtherCAT has no expected working count");
  auto status = ValidateProcessData(*data);
  if (!status.ok()) return status;
  return data;
}

void EthercatMaster::Run(const std::string& interface_name, ProcessDataMode mode) {
  runtime_sdo_ = options_.mailbox_step_budget.count() > 0 && io_->HasIncrementalSdo();
  auto status = options_.mailbox_step_budget.count() > 0 && !runtime_sdo_
                    ? absl::FailedPreconditionError("backend has no incremental SDO support")
                    : io_->Init(interface_name, mode);
  if (status.ok()) status = io_->ConfigureSlaves();
  if (status.ok()) {
    auto slaves = io_->GetSlaves();
    if (!slaves.ok())
      status = slaves.status();
    else if (slaves->empty() || slaves->size() > UINT16_MAX)
      status = absl::UnavailableError("invalid EtherCAT slave count");
    else {
      slaves_ = std::move(*slaves);
      for (size_t i = 0; i < slaves_.size(); ++i) {
        auto region = io_->GetPdoRegion(static_cast<uint16_t>(i + 1));
        if (!region.ok()) {
          status = region.status();
          break;
        }
        regions_.push_back(*region);
      }
    }
  }
  {
    std::lock_guard lock(mutex_);
    ready_ = true;
    if (!status.ok()) FailLocked(status);
  }
  cv_.notify_all();
  auto next_cycle = Clock::now();
  bool start_attempted = false;
  bool mailbox_turn = false;
  std::vector<Bytes> stop_images;
  while (true) {
    std::shared_ptr<Work> work;
    std::vector<Bytes> outputs;
    {
      std::unique_lock lock(mutex_);
      if (cyclic_)
        cv_.wait_until(lock, next_cycle, [&] { return stopping_; });
      else
        cv_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
      if (stopping_) break;
      if (cyclic_)
        outputs = outputs_;
      else {
        work = queue_.front();
        queue_.pop_front();
        if (work->done) continue;
        if (Clock::now() >= work->deadline) {
          FinishLocked(work, absl::DeadlineExceededError("expired before dispatch; not executed"));
          lock.unlock();
          cv_.notify_all();
          continue;
        }
        work->dispatched = true;
        active_ = work;
      }
    }
    if (!work) {
      // Process data first, then at most one mailbox OR state step. Alternation
      // prevents mailbox load from starving AL-state checks (at most two cycles).
      auto data = Exchange(outputs);
      status = data.status();
      next_cycle += options_.period;
      bool mailbox_pending = false;
      {
        std::lock_guard lock(mutex_);
        mailbox_pending = !stopping_ && runtime_sdo_ && (active_ || !queue_.empty());
      }
      if (status.ok()) {
        if (mailbox_turn && mailbox_pending) {
          MailboxStep(next_cycle);
        } else {
          const auto slack = std::chrono::duration_cast<Microseconds>(next_cycle - Clock::now()) -
                             options_.scheduling_guard;
          if (slack < options_.state_timeout)
            status = absl::DeadlineExceededError("insufficient cyclic slack for AL state check");
          else
            status = io_->CheckOperational(static_cast<int>(options_.state_timeout.count()));
        }
      }
      mailbox_turn = !mailbox_turn;
      if (status.ok() && Clock::now() >= next_cycle)
        status = absl::DeadlineExceededError("EtherCAT cyclic deadline missed");
      {
        std::lock_guard lock(mutex_);
        if (!status.ok())
          FailLocked(status);
        else if (!stopping_) {
          snapshot_.data = std::move(*data);
          ++snapshot_.sequence;
        }
      }
      cv_.notify_all();
      continue;
    }
    absl::StatusOr<Bytes> result = Bytes{};
    if (work->kind == Work::Kind::kStart) {
      start_attempted = true;
      stop_images = work->stop_images;
      for (size_t i = 0; i < regions_.size(); ++i) {
        status = io_->WriteOutputs(regions_[i], stop_images[i]);
        if (!status.ok()) break;
      }
      if (status.ok()) status = io_->StartCyclic();
      if (!status.ok()) result = status;
    } else {
      const auto remaining =
          std::chrono::duration_cast<Microseconds>(work->deadline - Clock::now());
      if (remaining.count() <= 0)
        result = absl::DeadlineExceededError("expired at dispatch");
      else if (work->kind == Work::Kind::kRead)
        result = io_->ReadSdo(work->address, work->capacity, static_cast<int>(remaining.count()));
      else {
        status = io_->WriteSdo(work->address, work->bytes, static_cast<int>(remaining.count()));
        if (!status.ok()) result = status;
      }
    }
    {
      std::lock_guard lock(mutex_);
      if (!stopping_ && Clock::now() >= work->deadline)
        result = absl::DeadlineExceededError("operation exceeded deadline; outcome unknown");
      // Fault on SDO failure/timeout, so a late reply cannot feed a later SDO.
      if (!stopping_ && !result.ok()) FailLocked(result.status());
      if (!stopping_ && work->kind == Work::Kind::kStart) {
        cyclic_ = true;
        outputs_ = stop_images;
        next_cycle = Clock::now();
        // Work queued before OP is not silently promoted to runtime work.
        for (const auto& queued : queue_)
          FinishLocked(queued, absl::FailedPreconditionError("resubmit SDO after cyclic startup"));
        queue_.clear();
      }
      FinishLocked(work, std::move(result));
      active_.reset();
    }
    cv_.notify_all();
  }
  io_->CancelSdo();
  if (start_attempted) {
    shutdown_status_ = Exchange(stop_images).status();
    const auto stopped = io_->StopCyclic();
    if (shutdown_status_.ok()) shutdown_status_ = stopped;
  }
  const auto teardown = io_->Teardown();
  if (shutdown_status_.ok()) shutdown_status_ = teardown;
  io_.reset();  // Backend destruction also belongs to the worker.
}

void EthercatMaster::MailboxStep(Clock::time_point next_cycle) {
  // Start only a complete budgeted step; no busy polling when slack is short.
  if (next_cycle - Clock::now() <= options_.mailbox_step_budget + options_.scheduling_guard) return;
  std::shared_ptr<Work> work;
  bool begin = false;
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;
    // Expired queued calls must never reach the backend. Queue length is capped.
    while (!active_ && !queue_.empty()) {
      auto candidate = queue_.front();
      queue_.pop_front();
      if (candidate->done) continue;
      if (Clock::now() >= candidate->deadline) {
        FinishLocked(candidate,
                     absl::DeadlineExceededError("expired before dispatch; not executed"));
        continue;
      }
      candidate->dispatched = true;
      active_ = candidate;
      begin = true;
    }
    work = active_;
  }
  cv_.notify_all();
  if (!work) return;
  absl::Status status;
  if (begin)
    status =
        io_->BeginSdo(work->address, work->kind == Work::Kind::kWrite, work->bytes, work->capacity);
  {
    std::lock_guard lock(mutex_);
    if (stopping_) return;  // No new datagram after cancellation during Begin.
  }
  const auto now = Clock::now();
  const auto budget = std::min(
      {options_.mailbox_step_budget,
       std::chrono::duration_cast<Microseconds>(next_cycle - now) - options_.scheduling_guard,
       std::chrono::duration_cast<Microseconds>(work->deadline - now)});
  absl::StatusOr<std::optional<Bytes>> result = std::optional<Bytes>{};
  if (!status.ok())
    result = status;
  else if (budget.count() <= 0)
    result = absl::DeadlineExceededError("runtime SDO expired; outcome unknown");
  else {
    result = io_->StepSdo(static_cast<int>(budget.count()));
    if (Clock::now() - now > budget) {
      // Preserve the backend's register/timing evidence when it also observed
      // the timeout. Replacing it with only "overrun" hides the failing I/O.
      const std::string detail = result.ok() ? "" : "; " + result.status().ToString();
      result = absl::DeadlineExceededError(
          "runtime SDO backend exceeded step budget; outcome unknown" + detail);
    }
  }
  {
    std::lock_guard lock(mutex_);
    if (!stopping_ && !result.ok())
      FailLocked(result.status());
    else if (!stopping_ && result->has_value()) {
      FinishLocked(work, std::move(**result));
      active_.reset();
    }
  }
  cv_.notify_all();
}

}  // namespace robot::comm::ethercat
