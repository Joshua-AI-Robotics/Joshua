#include "robot/comm/serial/serial_io_owner.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <cerrno>
#include <climits>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <filesystem>
#include <mutex>
#include <set>
#include <thread>

#include "robot/comm/serial/serial_config_validation.h"
#include "robot/comm/serial/serial_device_identity.h"

namespace robot::comm {
namespace {
using Clock = std::chrono::steady_clock;
// Process lifetime avoids teardown races with cached board capabilities.
struct Claims {
  std::mutex mutex;
  std::set<std::string> keys;
};
Claims& PortClaims() {
  static auto* claims = new Claims;
  return *claims;
}
std::atomic<uint64_t> next_generation{1};
uint64_t Micros(Clock::duration value) {
  return std::chrono::duration_cast<std::chrono::microseconds>(value).count();
}
}  // namespace

struct SerialIoOwner::Operation::State {
  Request request;
  Result result;
  Clock::time_point admitted;
  Clock::time_point deadline;
  Clock::time_point active_since;
  Clock::time_point last_read;
  size_t reservation = 0;
  bool prepared = false;
  std::atomic<bool> cancel{false};
  std::weak_ptr<Impl> owner;
  mutable std::mutex mutex;
  std::condition_variable changed;
  bool done = false;
};

struct SerialIoOwner::Impl {
  using Work = std::shared_ptr<Operation::State>;
  SerialIoOptions policy;
  std::optional<Receiver> receiver;
  PortIdentity identity;
  bool claimed = false;
  bool exclusive = false;
  std::unique_ptr<boost::asio::io_context> context;
  std::unique_ptr<boost::asio::serial_port> port;
  int wake_fd = -1;
  std::thread worker;
  std::thread::id worker_id;
  mutable std::mutex mutex;
  std::mutex close_mutex;
  std::deque<Work> queue;
  bool closing = false;
  bool running = false;
  SerialDiagnostics diagnostics;
  absl::Status fault;
  absl::Status close_status;

  ~Impl() {
    ClosePort();
    if (wake_fd >= 0) close(wake_fd);
    if (claimed) {
      auto& claims = PortClaims();
      std::lock_guard lock(claims.mutex);
      claims.keys.erase(identity.key);
    }
  }
  void Wake() {
    uint64_t one = 1;
    while (write(wake_fd, &one, sizeof(one)) < 0 && errno == EINTR) {
    }
    // EAGAIN means the owner already has a pending wakeup.
  }
  void DrainWake() {
    uint64_t value;
    while (read(wake_fd, &value, sizeof(value)) > 0) {
    }
  }
  void ClosePort() {
    if (!port || !port->is_open()) return;
    if (exclusive) ioctl(port->native_handle(), TIOCNXCL);
    boost::system::error_code error;
    port->close(error);
    if (error) close_status = absl::UnavailableError("Serial close failed: " + error.message());
  }
  // Called by the owner only. No completion callback executes on this thread.
  void Complete(const Work& work, absl::Status status) {
    work->request.bytes.clear();
    work->request.consume = {};
    {
      std::lock_guard lock(mutex);
      --diagnostics.pending_operations;
      diagnostics.pending_bytes -= work->reservation;
      ++diagnostics.completed;
      diagnostics.last_operation_us = Micros(Clock::now() - work->admitted);
      diagnostics.max_operation_us =
          std::max(diagnostics.max_operation_us, diagnostics.last_operation_us);
      if (absl::IsDeadlineExceeded(status)) ++diagnostics.timed_out;
      if (absl::IsCancelled(status)) ++diagnostics.cancelled;
      if (!status.ok()) diagnostics.last_error = status.ToString().substr(0, 512);
    }
    {
      std::lock_guard lock(work->mutex);
      work->result.status = std::move(status);
      work->done = true;
    }
    work->changed.notify_all();
  }
  void ExpireQueue() {
    std::vector<Work> expired;
    {
      std::lock_guard lock(mutex);
      const auto now = Clock::now();
      for (auto it = queue.begin(); it != queue.end();) {
        if ((*it)->cancel || now >= (*it)->deadline) {
          expired.push_back(*it);
          it = queue.erase(it);
        } else {
          ++it;
        }
      }
    }
    for (const auto& work : expired)
      Complete(work,
               work->cancel
                   ? absl::CancelledError("Serial operation canceled before dispatch")
                   : absl::DeadlineExceededError("Serial queue deadline; request not sent"));
  }
  void Fault(absl::Status status) {
    std::lock_guard lock(mutex);
    fault = std::move(status);
    diagnostics.state = SerialLinkState::kFaulted;
    diagnostics.last_error = fault.ToString().substr(0, 512);
    closing = true;
  }
  void Run() {
    Work active;
    for (;;) {
      ExpireQueue();
      {
        std::lock_guard lock(mutex);
        if (closing) break;
        if (!active && !queue.empty()) {
          active = queue.front();
          queue.pop_front();
          active->active_since = active->last_read = Clock::now();
          diagnostics.last_queue_wait_us = Micros(active->active_since - active->admitted);
          diagnostics.max_queue_wait_us =
              std::max(diagnostics.max_queue_wait_us, diagnostics.last_queue_wait_us);
        }
      }
      auto finish = [&](absl::Status status) {
        Complete(active, std::move(status));
        active.reset();
      };
      auto now = Clock::now();
      if (active && active->cancel) {
        if (receiver && active->result.bytes_written > 0 &&
            active->result.bytes_written < active->request.bytes.size()) {
          Fault(absl::UnavailableError("Cannot retire a partially written frame"));
          continue;
        }
        finish(absl::CancelledError("Serial operation canceled; inspect publication state"));
        continue;
      }
      if (active && now >= active->deadline) {
        if (receiver && active->result.bytes_written > 0 &&
            active->result.bytes_written < active->request.bytes.size()) {
          Fault(absl::UnavailableError("Partial frame write expired; link retired"));
          continue;
        }
        finish(absl::DeadlineExceededError("Serial operation deadline; inspect publication state"));
        continue;
      }
      if (active && !active->prepared) {
        try {
          if (!active->request.consume)
            active->result.response.reserve(active->request.response_bytes);
        } catch (const std::bad_alloc&) {
          finish(absl::ResourceExhaustedError("Cannot allocate bounded serial response"));
          continue;
        }
        if (active->request.input == InputFlushPolicy::kDiscard &&
            tcflush(port->native_handle(), TCIFLUSH) != 0) {
          Fault(absl::UnavailableError("Cannot flush serial input"));
          continue;
        }
        active->prepared = true;
      }
      bool reading = active && active->result.bytes_written == active->request.bytes.size();
      if (reading && !active->request.consume &&
          active->result.bytes_read == active->request.response_bytes) {
        finish(absl::OkStatus());
        continue;
      }
      if (reading && active->request.consume &&
          active->result.bytes_read == active->request.response_bytes) {
        finish(absl::ResourceExhaustedError("Serial response exceeds configured byte limit"));
        continue;
      }
      if (reading && active->request.read_idle_timeout.count() > 0 &&
          now >= active->last_read + active->request.read_idle_timeout) {
        finish(absl::DeadlineExceededError("Serial read idle deadline"));
        continue;
      }
      Clock::time_point deadline = Clock::time_point::max();
      if (active) {
        deadline = active->deadline;
        if (reading && active->request.read_idle_timeout.count() > 0)
          deadline = std::min(deadline, active->last_read + active->request.read_idle_timeout);
      }
      {
        std::lock_guard lock(mutex);
        for (const auto& work : queue) deadline = std::min(deadline, work->deadline);
      }
      const int timeout =
          deadline == Clock::time_point::max()
              ? -1
              : static_cast<int>(std::clamp<int64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())
                            .count() +
                        1,
                    0,
                    INT_MAX));
      pollfd descriptors[] = {{wake_fd, POLLIN, 0},
                              {port->native_handle(),
                               static_cast<short>((receiver ? POLLIN : 0) |
                                                  (active ? (reading ? POLLIN : POLLOUT) : 0)),
                               0}};
      const int result = poll(descriptors, 2, timeout);
      if (result < 0 && errno == EINTR) continue;
      if (result < 0 || (descriptors[1].revents & (POLLERR | POLLHUP | POLLNVAL))) {
        Fault(absl::UnavailableError("Serial connection lost; inspect publication state"));
        continue;
      }
      if (descriptors[0].revents) {
        DrainWake();
        continue;
      }
      if (result == 0) continue;
      if (receiver && (descriptors[1].revents & POLLIN)) {
        uint8_t bytes[256];
        const ssize_t count = read(port->native_handle(), bytes, sizeof(bytes));
        if (count > 0) {
          {
            std::lock_guard lock(mutex);
            diagnostics.bytes_read += count;
          }
          try {
            auto status = receiver->bytes(bytes, count);
            if (!status.ok()) {
              Fault(std::move(status));
              continue;
            }
          } catch (...) {
            Fault(absl::InternalError("Serial receiver threw"));
            continue;
          }
        } else if (count == 0 || (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)) {
          Fault(absl::UnavailableError("Continuous serial read failed"));
          continue;
        }
      }
      if (!active) continue;
      // Recheck cancellation/deadline after poll before any new publication.
      if (active->cancel || Clock::now() >= active->deadline) continue;
      if (!reading && (descriptors[1].revents & POLLOUT)) {
        const size_t offset = active->result.bytes_written;
        const ssize_t count = write(port->native_handle(),
                                    active->request.bytes.data() + offset,
                                    active->request.bytes.size() - offset);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) {
          Fault(absl::UnavailableError("Serial write failed"));
          continue;
        }
        active->result.bytes_written += count;
        active->result.publication = Publication::kMayHaveReachedDevice;
        active->last_read = Clock::now();
        std::lock_guard lock(mutex);
        diagnostics.bytes_written += count;
      } else if (reading && (descriptors[1].revents & POLLIN)) {
        uint8_t bytes[4096];
        const size_t remaining = active->request.response_bytes - active->result.bytes_read;
        if (remaining == 0) {
          finish(absl::ResourceExhaustedError("Serial response exceeds configured byte limit"));
          continue;
        }
        const size_t size = active->request.consume ? 1 : std::min(remaining, sizeof(bytes));
        const ssize_t count = read(port->native_handle(), bytes, size);
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        if (count <= 0) {
          Fault(absl::UnavailableError("Serial read failed"));
          continue;
        }
        active->result.bytes_read += count;
        active->last_read = Clock::now();
        {
          std::lock_guard lock(mutex);
          diagnostics.bytes_read += count;
        }
        if (active->request.consume) {
          try {
            auto complete = active->request.consume(bytes[0]);
            if (!complete.ok())
              finish(complete.status());
            else if (*complete)
              finish(absl::OkStatus());
          } catch (...) {
            finish(absl::InternalError("Serial framing callback threw"));
          }
        } else {
          active->result.response.insert(active->result.response.end(), bytes, bytes + count);
        }
      }
    }
    std::deque<Work> abandoned;
    absl::Status reason;
    {
      std::lock_guard lock(mutex);
      abandoned.swap(queue);
      reason = fault.ok() ? absl::CancelledError("Serial owner closed") : fault;
    }
    if (active) Complete(active, reason);
    for (const auto& work : abandoned) Complete(work, reason);
    ClosePort();
    if (receiver && receiver->stopped) {
      try {
        receiver->stopped(reason);
      } catch (...) {
        Fault(absl::InternalError("Serial stopped callback threw"));
      }
    }
    std::lock_guard lock(mutex);
    running = false;
    if (diagnostics.state != SerialLinkState::kFaulted)
      diagnostics.state = SerialLinkState::kClosed;
  }
};

absl::StatusOr<SerialIoOwner::PortIdentity> SerialIoOwner::Identify(const std::string& path) {
  auto identity = ResolveSerialDeviceIdentity(path);
  if (!identity.ok()) return identity.status();
  struct stat device {};
  if (stat(identity->path.c_str(), &device) != 0)
    return absl::UnavailableError("Error opening serial port " + identity->path + ": " +
                                  std::strerror(errno));
  if (!S_ISCHR(device.st_mode))
    return absl::InvalidArgumentError("Serial port must resolve to a character device");
  return PortIdentity{identity->path, identity->key};
}

absl::StatusOr<std::shared_ptr<SerialIoOwner>> SerialIoOwner::Open(
    const std::string& path,
    int baudrate,
    std::chrono::milliseconds settle,
    SerialIoOptions options,
    std::optional<Receiver> receiver) {
  if (baudrate <= 0 || settle.count() < 0 || settle.count() > INT_MAX)
    return absl::InvalidArgumentError("Invalid serial baudrate/settle policy");
  auto valid = ValidateSerialOptions(options);
  if (!valid.ok()) return valid;
  if (receiver && (!receiver->bytes || receiver->max_write_bytes == 0 ||
                   receiver->max_write_bytes > options.max_transfer_bytes))
    return absl::InvalidArgumentError("Invalid continuous serial receiver policy");
  auto identified = Identify(path);
  if (!identified.ok()) return identified.status();
  auto state = std::make_shared<Impl>();
  state->identity = *identified;
  state->policy = options;
  state->receiver = std::move(receiver);
  {
    auto& claims = PortClaims();
    std::lock_guard lock(claims.mutex);
    if (!claims.keys.insert(identified->key).second)
      return absl::AlreadyExistsError("Physical serial port already has an owner");
    state->claimed = true;
  }
  try {
    state->context = std::make_unique<boost::asio::io_context>();
    state->port =
        std::make_unique<boost::asio::serial_port>(*state->context, identified->canonical_path);
    const int fd = state->port->native_handle();
    struct stat opened {};
    if (fstat(fd, &opened) != 0 ||
        identified->key != "character:" + std::to_string(static_cast<uintmax_t>(opened.st_rdev)))
      return absl::UnavailableError("Serial device changed while opening");
    if (flock(fd, LOCK_EX | LOCK_NB) != 0 || ioctl(fd, TIOCEXCL) != 0)
      return absl::AlreadyExistsError("Serial port is claimed by another owner");
    state->exclusive = true;
    state->port->set_option(boost::asio::serial_port_base::baud_rate(baudrate));
    state->port->set_option(boost::asio::serial_port_base::character_size(8));
    state->port->set_option(
        boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
    state->port->set_option(
        boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
    state->port->set_option(boost::asio::serial_port_base::flow_control(
        boost::asio::serial_port_base::flow_control::none));
    const int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) != 0)
      return absl::UnavailableError("Cannot configure nonblocking serial descriptor");
    state->wake_fd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    if (state->wake_fd < 0) return absl::UnavailableError("Cannot create serial owner wakeup");
    std::this_thread::sleep_for(settle);
    state->diagnostics.configured_port = path;
    state->diagnostics.canonical_port = identified->canonical_path;
    state->diagnostics.generation = next_generation.fetch_add(1);
    state->diagnostics.state = SerialLinkState::kReady;
    auto owner = std::shared_ptr<SerialIoOwner>(new SerialIoOwner(state));
    state->running = true;
    state->worker = std::thread([state] { state->Run(); });
    state->worker_id = state->worker.get_id();
    return owner;
  } catch (const std::exception& error) {
    return absl::UnavailableError(std::string("Cannot open serial owner: ") + error.what());
  }
}

SerialIoOwner::~SerialIoOwner() {
  Close().IgnoreError();
}
SerialIoOwner::Operation SerialIoOwner::Submit(Request request) {
  auto work = std::make_shared<Operation::State>();
  work->admitted = Clock::now();
  work->request = std::move(request);
  auto& r = work->request;
  absl::Status rejected;
  std::lock_guard lock(impl_->mutex);
  if (impl_->running && std::this_thread::get_id() == impl_->worker_id)
    rejected = absl::FailedPreconditionError("Serial framing cannot reenter its I/O owner");
  else if (impl_->closing)
    rejected =
        impl_->fault.ok() ? absl::FailedPreconditionError("Serial owner is closed") : impl_->fault;
  else if (impl_->receiver &&
           (r.response_bytes != 0 || r.consume || r.input != InputFlushPolicy::kPreserve ||
            r.bytes.size() > impl_->receiver->max_write_bytes))
    rejected =
        absl::FailedPreconditionError("Continuous receiver owns all input; frame writes only");
  else if (r.timeout.count() <= 0 || r.timeout.count() > INT_MAX ||
           r.read_idle_timeout.count() < 0 || r.read_idle_timeout.count() > INT_MAX ||
           (r.consume && r.response_bytes == 0) ||
           (r.input != InputFlushPolicy::kDiscard && r.input != InputFlushPolicy::kPreserve))
    rejected = absl::InvalidArgumentError("Invalid serial operation/deadline");
  else if (r.deadline && *r.deadline <= work->admitted)
    rejected = absl::DeadlineExceededError("Serial absolute deadline expired before admission");
  else if (r.bytes.size() > impl_->policy.max_transfer_bytes ||
           r.response_bytes > impl_->policy.max_transfer_bytes - r.bytes.size())
    rejected = absl::ResourceExhaustedError("Serial transfer exceeds configured byte limit");
  else {
    work->reservation = r.bytes.size() + r.response_bytes;
    if (impl_->diagnostics.pending_operations >= impl_->policy.max_pending_operations ||
        work->reservation > impl_->policy.max_pending_bytes - impl_->diagnostics.pending_bytes)
      rejected = absl::ResourceExhaustedError("Serial admission queue is full");
  }
  if (!rejected.ok()) {
    ++impl_->diagnostics.rejected;
    work->result.status = rejected;
    work->done = true;
  } else {
    work->deadline = work->admitted + r.timeout;
    if (r.deadline) work->deadline = std::min(work->deadline, *r.deadline);
    work->owner = impl_;
    try {
      impl_->queue.push_back(work);
    } catch (const std::bad_alloc&) {
      ++impl_->diagnostics.rejected;
      work->result.status = absl::ResourceExhaustedError("Cannot allocate serial queue entry");
      work->done = true;
      return Operation(std::move(work));
    }
    ++impl_->diagnostics.pending_operations;
    impl_->diagnostics.pending_bytes += work->reservation;
    ++impl_->diagnostics.admitted;
    impl_->Wake();
  }
  return Operation(std::move(work));
}
void SerialIoOwner::Operation::Cancel() const {
  {
    std::lock_guard lock(state_->mutex);
    if (state_->done) return;
  }
  state_->cancel = true;
  if (auto owner = state_->owner.lock()) owner->Wake();
}
bool SerialIoOwner::Operation::Ready() const {
  std::lock_guard lock(state_->mutex);
  return state_->done;
}
SerialIoOwner::Result SerialIoOwner::Operation::Wait() const {
  std::unique_lock lock(state_->mutex);
  state_->changed.wait(lock, [&] { return state_->done; });
  return state_->result;
}
SerialDiagnostics SerialIoOwner::Snapshot() const {
  std::lock_guard lock(impl_->mutex);
  return impl_->diagnostics;
}
absl::Status SerialIoOwner::status() const {
  std::lock_guard lock(impl_->mutex);
  if (!impl_->fault.ok()) return impl_->fault;
  return impl_->closing ? absl::FailedPreconditionError("Serial owner is closed")
                        : absl::OkStatus();
}
absl::Status SerialIoOwner::Close() {
  {
    std::lock_guard lock(impl_->mutex);
    // A joined worker's thread ID can be reused by an unrelated caller.
    if (impl_->running && std::this_thread::get_id() == impl_->worker_id)
      return absl::FailedPreconditionError(
          "Serial owner cannot close itself from a framing callback");
  }
  std::lock_guard close_lock(impl_->close_mutex);
  {
    std::lock_guard lock(impl_->mutex);
    impl_->closing = true;
  }
  impl_->Wake();
  if (impl_->worker.joinable()) impl_->worker.join();
  return impl_->close_status;
}
const SerialIoOptions& SerialIoOwner::options() const {
  return impl_->policy;
}
const SerialIoOwner::PortIdentity& SerialIoOwner::identity() const {
  return impl_->identity;
}
}  // namespace robot::comm
