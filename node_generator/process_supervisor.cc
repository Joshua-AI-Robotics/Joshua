#include "node_generator/process_supervisor.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <map>
#include <stdexcept>
#include <utility>

#include "utils/joshua_runtime.h"

extern char** environ;

namespace node_generator {
namespace {
using SteadyClock = std::chrono::steady_clock;
constexpr int kTickMs = 10;
volatile sig_atomic_t stop_signal = 0;

void RequestStop(int signal) { stop_signal = signal; }
void IgnoreSignal(int) {}

void ParentDied(int) {
  // The guardian never execs, so this handler survives for its whole lifetime.
  // Its group contains only this job and descendants that obey group ownership.
  kill(-getpid(), SIGKILL);
  _exit(1);
}

struct Event {
  int kind;  // 1 = application PID, 2 = application wait status.
  int value;
};

void WriteAll(int fd, const void* data, size_t size) {
  const auto* bytes = static_cast<const char*>(data);
  while (size) {
    const ssize_t count = write(fd, bytes, size);
    if (count < 0 && errno == EINTR) continue;
    if (count <= 0) return;
    bytes += count;
    size -= count;
  }
}

// Before forking the executable, discard other jobs' inherited control fds.
void CloseExcept(const std::array<int, 3>& keep, unsigned int limit) {
  unsigned int first = 3;
  for (int fd : keep) {
    if (static_cast<unsigned int>(fd) > first) {
#ifdef SYS_close_range
      if (syscall(SYS_close_range, first, fd - 1, 0) != 0)
#endif
        for (unsigned int next = first; next < static_cast<unsigned int>(fd); ++next) close(next);
    }
    first = fd + 1;
  }
#ifdef SYS_close_range
  if (syscall(SYS_close_range, first, ~0U, 0) == 0) return;
#endif
  for (unsigned int fd = first; fd < limit; ++fd) close(fd);
}

// After fork, this path uses only system calls and precomputed arguments.
[[noreturn]] void GuardProcess(pid_t owner,
                               const ProcessSpec& spec,
                               char* const* arguments,
                               char* const* environment,
                               int events,
                               int exec_error,
                               int ready,
                               const std::array<int, 3>& keep,
                               unsigned int fd_limit) {
  CloseExcept(keep, fd_limit);
  struct sigaction action{};
  sigemptyset(&action.sa_mask);
  action.sa_handler = ParentDied;
  if (setpgid(0, 0) < 0 || sigaction(SIGUSR2, &action, nullptr) < 0 ||
      prctl(PR_SET_PDEATHSIG, SIGUSR2) < 0) {
    const int error = errno;
    WriteAll(exec_error, &error, sizeof(error));
    _exit(1);
  }
  if (getppid() != owner) ParentDied(0);
  action.sa_handler = IgnoreSignal;
  sigaction(SIGINT, &action, nullptr);
  sigaction(SIGTERM, &action, nullptr);
  action.sa_handler = SIG_IGN;
  sigaction(SIGPIPE, &action, nullptr);
  sigset_t mask;
  sigemptyset(&mask);
  sigprocmask(SIG_SETMASK, &mask, nullptr);

  const pid_t guardian = getpid();
  const pid_t application = fork();
  if (application < 0) {
    const int error = errno;
    WriteAll(exec_error, &error, sizeof(error));
    _exit(1);
  }
  if (application == 0) {
    close(events);
    action.sa_handler = SIG_DFL;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGUSR2, &action, nullptr);
    sigaction(SIGPIPE, &action, nullptr);
    if (prctl(PR_SET_PDEATHSIG, SIGKILL) < 0 || getppid() != guardian ||
        (!spec.working_directory.empty() && chdir(spec.working_directory.c_str()) < 0) ||
        (spec.require_ready && fcntl(ready, F_SETFD, 0) < 0)) {
      const int error = errno ? errno : ECHILD;
      WriteAll(exec_error, &error, sizeof(error));
      _exit(1);
    }
    if (!spec.require_ready) close(ready);
    execve(spec.executable.c_str(), arguments, environment);
    const int error = errno;
    WriteAll(exec_error, &error, sizeof(error));
    _exit(1);
  }
  close(exec_error);
  close(ready);
  const Event started{1, application};
  WriteAll(events, &started, sizeof(started));
  int status;
  for (;;) {
    const pid_t result = waitpid(application, &status, 0);
    if (result < 0 && errno == EINTR) continue;
    if (result != application) _exit(1);
    const Event exited{2, status};
    WriteAll(events, &exited, sizeof(exited));
    close(events);
    // Keep the group identity reserved until the owner cleans up descendants.
    for (;;) pause();
  }
}

class Fd {
 public:
  explicit Fd(int value = -1) : value_(value) {}
  ~Fd() { Reset(); }
  Fd(const Fd&) = delete;
  Fd& operator=(const Fd&) = delete;
  int get() const { return value_; }
  void Reset(int value = -1) {
    if (value_ >= 0) close(value_);
    value_ = value;
  }
 private:
  int value_;
};

absl::Status Pipe(Fd& read_end, Fd& write_end) {
  int descriptors[2];
  if (pipe2(descriptors, O_CLOEXEC) < 0) return absl::ErrnoToStatus(errno, "Cannot create child pipe");
  read_end.Reset(descriptors[0]);
  write_end.Reset(descriptors[1]);
  for (Fd* end : {&read_end, &write_end}) {
    if (end->get() >= 3) continue;
    const int copy = fcntl(end->get(), F_DUPFD_CLOEXEC, 3);
    if (copy < 0) return absl::ErrnoToStatus(errno, "Cannot reserve child control fd");
    end->Reset(copy);
  }
  const int flags = fcntl(read_end.get(), F_GETFL);
  if (flags < 0 || fcntl(read_end.get(), F_SETFL, flags | O_NONBLOCK) < 0)
    return absl::ErrnoToStatus(errno, "Cannot configure child pipe");
  return absl::OkStatus();
}
}  // namespace

class ProcessSupervisor::Child {
 public:
  explicit Child(ProcessSpec spec) : spec_(std::move(spec)) { snapshot_.name = spec_.name; }
  ~Child() {
    if (snapshot_.guardian_pid > 0 && !snapshot_.reaped) {
      // A final fallback; normal callers use the bounded shutdown sequence.
      if (!group_killed_) kill(-snapshot_.process_group, SIGKILL);
      if (!guardian_reaped_) kill(snapshot_.guardian_pid, SIGKILL);
      const auto deadline = SteadyClock::now() + std::chrono::seconds(1);
      while (!snapshot_.reaped && SteadyClock::now() < deadline) {
        Reap().IgnoreError();
        if (!snapshot_.reaped) poll(nullptr, 0, kTickMs);
      }
    }
  }

  absl::Status Start() {
    Fd event_write, error_write, ready_write;
    auto status = Pipe(events_, event_write);
    if (!status.ok()) return status;
    status = Pipe(exec_error_, error_write);
    if (!status.ok()) return status;
    status = Pipe(ready_, ready_write);
    if (!status.ok()) return status;

    std::map<std::string, std::string> env;
    for (char** next = environ; *next; ++next) {
      const std::string entry(*next);
      const auto equal = entry.find('=');
      if (equal != std::string::npos) env[entry.substr(0, equal)] = entry.substr(equal + 1);
    }
    for (const auto& name : spec_.unset_environment) env.erase(name);
    for (const auto& item : spec_.environment) env[item.first] = item.second;
    env.erase("JOSHUA_READY_FD");
    if (spec_.require_ready) env["JOSHUA_READY_FD"] = std::to_string(ready_write.get());
    std::vector<std::string> env_strings;
    for (const auto& item : env) env_strings.push_back(item.first + "=" + item.second);
    std::vector<char*> envp, argv;
    for (auto& entry : env_strings) envp.push_back(entry.data());
    envp.push_back(nullptr);
    argv.push_back(spec_.executable.data());
    for (auto& arg : spec_.arguments) argv.push_back(arg.data());
    argv.push_back(nullptr);
    std::array<int, 3> keep{event_write.get(), error_write.get(), ready_write.get()};
    std::sort(keep.begin(), keep.end());
    struct rlimit limit{};
    if (getrlimit(RLIMIT_NOFILE, &limit) != 0) return absl::ErrnoToStatus(errno, "Descriptor limit");
    const pid_t owner = getpid();
    const pid_t guardian = fork();
    if (guardian < 0) return absl::ErrnoToStatus(errno, "Cannot fork node guardian");
    if (guardian == 0) {
      GuardProcess(owner, spec_, argv.data(), envp.data(), event_write.get(), error_write.get(),
                   ready_write.get(), keep, static_cast<unsigned int>(limit.rlim_cur));
    }
    snapshot_.guardian_pid = guardian;
    snapshot_.process_group = guardian;
    snapshot_.state = ProcessState::kStarting;
    // Establish the group from both sides before returning a controllable job.
    if (setpgid(guardian, guardian) < 0 && errno != EACCES && errno != ESRCH)
      return absl::ErrnoToStatus(errno, "Cannot establish node process group");
    return absl::OkStatus();
  }

  void StartupFailed(const absl::Status& error) {
    Fail(error).IgnoreError();
    if (snapshot_.guardian_pid <= 0) {
      snapshot_.exited = true;
      snapshot_.reaped = true;
    }
  }

  absl::Status Poll() {
    if (!error_.ok() && !stopping_) return error_;
    if (snapshot_.guardian_pid <= 0 || snapshot_.reaped) return absl::OkStatus();
    if (!snapshot_.executable_started && exec_error_.get() >= 0) {
      int error;
      const ssize_t count = read(exec_error_.get(), &error, sizeof(error));
      if (count == sizeof(error)) {
        exec_error_.Reset();
        return Fail(absl::ErrnoToStatus(error, "Cannot exec " + spec_.name));
      }
      if (count == 0) {
        snapshot_.executable_started = true;
        if (!stopping_ && snapshot_.error.empty()) snapshot_.state = ProcessState::kRunning;
        exec_error_.Reset();
      } else if (count < 0 && errno != EAGAIN && errno != EINTR) {
        return Fail(absl::ErrnoToStatus(errno, "Exec status pipe"));
      }
    }
    for (;;) {
      Event event{};
      const ssize_t count = read(events_.get(), &event, sizeof(event));
      if (count < 0 && (errno == EAGAIN || errno == EINTR)) break;
      if (count == 0) break;
      if (count != sizeof(event)) return Fail(absl::DataLossError("Incomplete process event"));
      if (event.kind == 1) {
        snapshot_.pid = event.value;
        std::fprintf(stderr, "Joshua: %s pid=%d guardian=%d group=%d\n", spec_.name.c_str(),
                     snapshot_.pid, snapshot_.guardian_pid, snapshot_.process_group);
      }
      if (event.kind == 2) {
        snapshot_.exited = true;
        snapshot_.exit_code = WIFEXITED(event.value) ? WEXITSTATUS(event.value) : -1;
        snapshot_.termination_signal = WIFSIGNALED(event.value) ? WTERMSIG(event.value) : 0;
        if (snapshot_.error.empty())
          snapshot_.state = snapshot_.exit_code == 0 ? ProcessState::kExited : ProcessState::kFailed;
        snapshot_.expected_exit = snapshot_.error.empty() &&
            (stopping_ || (spec_.allow_completion && snapshot_.exit_code == 0));
        if (!snapshot_.expected_exit)
          return Fail(absl::UnavailableError("Required node exited unexpectedly: " + spec_.name +
              " exit=" + std::to_string(snapshot_.exit_code) +
              " signal=" + std::to_string(snapshot_.termination_signal)));
      }
    }
    if (!snapshot_.ready && ready_.get() >= 0) {
      char ready;
      const ssize_t count = read(ready_.get(), &ready, 1);
      if (count == 1 && ready == 'R') {
        snapshot_.ready = true;
        if (!stopping_ && !snapshot_.exited && snapshot_.error.empty())
          snapshot_.state = ProcessState::kReady;
        ready_.Reset();
      } else if (count == 0 && spec_.require_ready && !stopping_) {
        return Fail(absl::UnavailableError("Node closed readiness pipe before initialization: " + spec_.name));
      } else if (count > 0 || (count < 0 && errno != EAGAIN && errno != EINTR)) {
        return Fail(absl::DataLossError("Invalid node readiness notification: " + spec_.name));
      }
      if (count == 0) ready_.Reset();
    }
    // WNOWAIT reserves the guardian's PID/PGID until group cleanup is complete.
    if (guardian_reaped_) return absl::OkStatus();
    siginfo_t info{};
    if (waitid(P_PID, snapshot_.guardian_pid, &info, WEXITED | WNOHANG | WNOWAIT) < 0)
      return Fail(absl::ErrnoToStatus(errno, "Cannot observe node guardian"));
    if (info.si_pid && !stopping_)
      return Fail(absl::UnavailableError("Node guardian exited unexpectedly: " + spec_.name));
    return absl::OkStatus();
  }

  absl::Status Signal(int signal) {
    stopping_ = true;
    if (snapshot_.guardian_pid <= 0 || snapshot_.reaped || group_killed_) return absl::OkStatus();
    if (signal == SIGKILL && !snapshot_.exited) snapshot_.forced_shutdown = true;
    if (!snapshot_.exited && snapshot_.error.empty()) snapshot_.state = ProcessState::kStopping;
    if (kill(-snapshot_.process_group, signal) < 0 && errno != ESRCH)
      return absl::ErrnoToStatus(errno, "Cannot signal node group " + spec_.name);
    if (signal == SIGKILL) group_killed_ = true;
    return absl::OkStatus();
  }

  absl::Status Reap() {
    if (snapshot_.guardian_pid <= 0 || snapshot_.reaped) return absl::OkStatus();
    for (;;) {
      int status;
      // The launcher is a subreaper: adopted descendants are reaped only from
      // this owned group, never through waitpid(-1).
      const pid_t result = waitpid(-snapshot_.process_group, &status, WNOHANG);
      if (result > 0) {
        if (result == snapshot_.guardian_pid) guardian_reaped_ = true;
        continue;
      }
      if (result < 0 && errno == EINTR) continue;
      if (result < 0 && errno != ECHILD) return absl::ErrnoToStatus(errno, "Cannot reap node group");
      const bool group_gone = kill(-snapshot_.process_group, 0) < 0 && errno == ESRCH;
      snapshot_.reaped = guardian_reaped_ && result < 0 && group_gone;
      break;
    }
    if (snapshot_.reaped) {
      if (!snapshot_.exited) {
        snapshot_.exited = true;
        snapshot_.expected_exit = stopping_ && snapshot_.error.empty();
        snapshot_.state = snapshot_.error.empty() ? ProcessState::kExited : ProcessState::kFailed;
      }
    }
    return absl::OkStatus();
  }

  const ProcessSnapshot& snapshot() const { return snapshot_; }
  bool StartupComplete() const {
    return snapshot_.pid > 0 && snapshot_.executable_started && (!spec_.require_ready || snapshot_.ready);
  }

 private:
  absl::Status Fail(const absl::Status& error) {
    snapshot_.state = ProcessState::kFailed;
    if (snapshot_.error.empty()) snapshot_.error = error.ToString();
    if (error_.ok()) error_ = error;
    return error;
  }
  ProcessSpec spec_;
  ProcessSnapshot snapshot_;
  absl::Status error_;
  Fd events_, exec_error_, ready_;
  bool stopping_ = false;
  bool guardian_reaped_ = false;
  bool group_killed_ = false;
};

ProcessSupervisor::ProcessSupervisor() {
  if (prctl(PR_SET_CHILD_SUBREAPER, 1) < 0)
    throw std::runtime_error("Cannot enable managed-process subreaping");
}
ProcessSupervisor::~ProcessSupervisor() {
  joshua::RunCleanup("process supervisor cleanup", [&] { joshua::ThrowIfError(Shutdown()); });
}

absl::Status ProcessSupervisor::Start(const ProcessSpec& spec) {
  if (stop_signal || joshua::ShutdownRequested()) return absl::CancelledError("Process startup interrupted");
  if (spec.name.empty() || spec.executable.empty()) return absl::InvalidArgumentError("Empty process spec");
  for (const auto& child : children_)
    if (child->snapshot().name == spec.name) return absl::AlreadyExistsError("Duplicate node name");
  auto child = std::make_unique<Child>(spec);
  const auto status = child->Start();
  if (!status.ok()) child->StartupFailed(status);
  children_.push_back(std::move(child));
  return status;
}

absl::Status ProcessSupervisor::AwaitReady(std::chrono::milliseconds timeout) {
  if (timeout.count() < 0) return absl::InvalidArgumentError("Negative readiness timeout");
  const auto deadline = SteadyClock::now() + timeout;
  for (;;) {
    if (stop_signal || joshua::ShutdownRequested()) return absl::CancelledError("Startup interrupted");
    const auto status = Poll();
    if (!status.ok()) return status;
    bool all_started = true;
    for (const auto& child : children_) all_started &= child->StartupComplete();
    if (all_started) return absl::OkStatus();
    if (SteadyClock::now() >= deadline) {
      for (const auto& child : children_) {
        if (!child->StartupComplete())
          child->StartupFailed(absl::DeadlineExceededError("Readiness timed out: " + child->snapshot().name));
      }
      return absl::DeadlineExceededError("Node readiness timed out");
    }
    poll(nullptr, 0, kTickMs);
  }
}

absl::Status ProcessSupervisor::Poll() {
  absl::Status result;
  for (const auto& child : children_) {
    const auto status = child->Poll();
    if (result.ok() && !status.ok()) result = status;
  }
  return result;
}

bool ProcessSupervisor::HasLiveProcesses() const {
  for (const auto& child : children_)
    if (!child->snapshot().exited && !child->snapshot().reaped) return true;
  return false;
}

absl::Status ProcessSupervisor::Shutdown(const ShutdownPolicy& policy) {
  if (policy.interrupt_grace.count() < 0 || policy.terminate_grace.count() < 0 ||
      policy.kill_grace.count() < 0) return absl::InvalidArgumentError("Negative shutdown grace period");
  absl::Status result;
  auto remember = [&](const absl::Status& status) { if (result.ok() && !status.ok()) result = status; };
  auto signal = [&](int value) { for (const auto& child : children_) remember(child->Signal(value)); };
  auto wait = [&](std::chrono::milliseconds duration, bool reaping) {
    const auto deadline = SteadyClock::now() + duration;
    do {
      bool complete = true;
      for (const auto& child : children_) {
        remember(child->Poll());
        if (reaping) remember(child->Reap());
        complete &= reaping ? child->snapshot().reaped : child->snapshot().exited;
      }
      if (complete) return true;
      poll(nullptr, 0, kTickMs);
    } while (SteadyClock::now() < deadline);
    return false;
  };
  signal(SIGINT);
  if (!wait(policy.interrupt_grace, false)) {
    signal(SIGTERM);
    wait(policy.terminate_grace, false);
  }
  // Guardians reserve the group until this last sweep, including after a clean
  // application exit. Descendants must remain in their assigned process group.
  signal(SIGKILL);
  if (!wait(policy.kill_grace, true) && result.ok())
    result = absl::DeadlineExceededError("Node guardians have not all been reaped");
  return result;
}

std::vector<ProcessSnapshot> ProcessSupervisor::Snapshots() const {
  std::vector<ProcessSnapshot> result;
  for (const auto& child : children_) result.push_back(child->snapshot());
  return result;
}

class SupervisorSignals::Impl {
 public:
  struct sigaction old_int{}, old_term{};
};

SupervisorSignals::SupervisorSignals() : impl_(std::make_unique<Impl>()) {
  stop_signal = 0;
  struct sigaction action{};
  action.sa_handler = RequestStop;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGINT, &action, &impl_->old_int) < 0) throw std::runtime_error("SIGINT handler");
  if (sigaction(SIGTERM, &action, &impl_->old_term) < 0) {
    sigaction(SIGINT, &impl_->old_int, nullptr);
    throw std::runtime_error("SIGTERM handler");
  }
}

SupervisorSignals::~SupervisorSignals() {
  sigaction(SIGINT, &impl_->old_int, nullptr);
  sigaction(SIGTERM, &impl_->old_term, nullptr);
}

bool SupervisorSignals::Requested() const noexcept { return stop_signal != 0; }

bool SupervisorShutdownRequested() noexcept { return stop_signal != 0; }

}  // namespace node_generator
