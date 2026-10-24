#include "utils/joshua_runtime.h"

#include <fcntl.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <utility>

#include "glog/logging.h"
#include "config/proto/config.pb.h"
#include "utils/robot_time.h"

namespace joshua {
namespace {
struct RuntimeState {
  std::atomic<bool> stop{false};
  std::atomic<bool> failed{false};
  std::once_flag logging;
  std::mutex ready_mutex;
  bool ready_sent = false;
};

RuntimeState& State() {
  // Workers and static driver destructors can still report errors at exit.
  static auto* state = new RuntimeState;
  return *state;
}

thread_local bool in_process_guard = false;
thread_local bool in_cleanup = false;
}  // namespace

bool internal::SetCleanupActive(bool active) noexcept {
  const bool previous = in_cleanup;
  in_cleanup = active;
  return previous;
}

void JoshuaInit(const config::Config& config) {
  auto clock = MakeRobotClock(config.general().robot_clock());
  if (!clock.ok()) throw std::runtime_error("Failed to create robot clock: " + clock.status().ToString());
  const std::string source((*clock)->SourceName());
  const auto status = SetGlobalRobotClock(std::move(*clock));
  ThrowIfError(status);
  LOG(INFO) << "Robot clock initialized: " << source;
}

void JoshuaInit() {
  JoshuaInit(config::Config{});
}

void ThrowIfError(const absl::Status& status) {
  if (!status.ok()) throw std::runtime_error(status.ToString());
}

bool ShutdownRequested() noexcept {
  return State().stop.load(std::memory_order_acquire);
}

bool RuntimeFailed() noexcept {
  return State().failed.load(std::memory_order_acquire);
}

bool CancellationRequested() noexcept {
  return ShutdownRequested() && !in_cleanup;
}

void RequestShutdown() noexcept {
  State().stop.store(true, std::memory_order_release);
}

void ReportFailure(const char* context, std::exception_ptr error) noexcept {
  State().failed.store(true, std::memory_order_release);
  RequestShutdown();
  try {
    if (error) std::rethrow_exception(error);
    std::fprintf(stderr, "Joshua: %s failed\n", context);
  } catch (const std::exception& exception) {
    std::fprintf(stderr, "Joshua: %s: %s\n", context, exception.what());
  } catch (...) {
    std::fprintf(stderr, "Joshua: %s: unknown exception\n", context);
  }
}

int RunGuarded(const char* name, const std::function<int()>& application) {
  if (in_process_guard) return application();
  in_process_guard = true;
  struct Reset {
    ~Reset() { in_process_guard = false; }
  } reset;
  State().stop.store(false, std::memory_order_relaxed);
  State().failed.store(false, std::memory_order_relaxed);
  int result = 1;
  try {
    std::call_once(State().logging, [name] {
      google::InitGoogleLogging(name);
      FLAGS_logtostderr = 1;
    });
    result = application();
  } catch (...) {
    ReportFailure(name, std::current_exception());
  }
  return result != 0 ? result : (RuntimeFailed() ? 1 : 0);
}

std::thread StartWorker(std::string name, std::function<void()> work) {
  return std::thread([name = std::move(name), work = std::move(work), cleanup = in_cleanup] {
    internal::CleanupScope scope(cleanup);
    try {
      work();
    } catch (...) {
      ReportFailure(name.c_str(), std::current_exception());
    }
  });
}

absl::Status NotifyReady() {
  std::lock_guard<std::mutex> lock(State().ready_mutex);
  if (State().ready_sent) return absl::OkStatus();
  const char* value = std::getenv("JOSHUA_READY_FD");
  if (!value) return absl::OkStatus();
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(value, &end, 10);
  if (errno || end == value || *end || parsed < 3 || parsed > std::numeric_limits<int>::max()) {
    return absl::InvalidArgumentError("Invalid JOSHUA_READY_FD");
  }
  const int fd = static_cast<int>(parsed);
  // Children of the application must not retain the startup channel.
  if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return absl::ErrnoToStatus(errno, "Ready fd");
  const char ready = 'R';
  ssize_t written;
  do {
    written = write(fd, &ready, 1);
  } while (written < 0 && errno == EINTR);
  const int error = errno;
  close(fd);
  unsetenv("JOSHUA_READY_FD");
  if (written != 1) return absl::ErrnoToStatus(error, "Cannot notify launcher readiness");
  State().ready_sent = true;
  return absl::OkStatus();
}

}  // namespace joshua
