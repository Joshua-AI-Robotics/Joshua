#pragma once

#include <sys/types.h>

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/status/status.h"

namespace node_generator {

bool SupervisorShutdownRequested() noexcept;

enum class ProcessState { kSpawning, kStarting, kRunning, kReady, kStopping, kExited, kFailed };

struct ProcessSpec {
  std::string name;
  std::string executable;
  std::vector<std::string> arguments;
  std::vector<std::pair<std::string, std::string>> environment;
  std::vector<std::string> unset_environment;
  std::string working_directory;
  bool require_ready = true;
  bool allow_completion = false;
};

struct ProcessSnapshot {
  std::string name;
  pid_t pid = -1;  // Application PID.
  pid_t guardian_pid = -1;
  pid_t process_group = -1;
  ProcessState state = ProcessState::kSpawning;
  bool executable_started = false;
  bool ready = false;
  bool exited = false;
  bool expected_exit = false;
  bool reaped = false;
  bool forced_shutdown = false;  // Application was still live at forced escalation.
  int exit_code = -1;
  int termination_signal = 0;
  std::string error;
};

struct ShutdownPolicy {
  std::chrono::milliseconds interrupt_grace{3000};
  std::chrono::milliseconds terminate_grace{2000};
  std::chrono::milliseconds kill_grace{1000};
};

// Owns logical node jobs, including guardians and their process groups. Only
// this object signals/reaps its guardians; snapshots retain exit history.
class ProcessSupervisor {
 public:
  ProcessSupervisor();
  ~ProcessSupervisor();
  ProcessSupervisor(const ProcessSupervisor&) = delete;
  ProcessSupervisor& operator=(const ProcessSupervisor&) = delete;

  absl::Status Start(const ProcessSpec& spec);
  absl::Status AwaitReady(std::chrono::milliseconds timeout = std::chrono::seconds(30));
  absl::Status Poll();
  bool HasLiveProcesses() const;
  absl::Status Shutdown(const ShutdownPolicy& policy = {});
  std::vector<ProcessSnapshot> Snapshots() const;

 private:
  class Child;
  std::vector<std::unique_ptr<Child>> children_;
};

// Scoped launcher-only signal handlers; normal control flow does the cleanup.
class SupervisorSignals {
 public:
  SupervisorSignals();
  ~SupervisorSignals();
  SupervisorSignals(const SupervisorSignals&) = delete;
  SupervisorSignals& operator=(const SupervisorSignals&) = delete;
  bool Requested() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace node_generator
