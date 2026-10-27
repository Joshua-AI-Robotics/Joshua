#pragma once

#include <exception>
#include <functional>
#include <string>
#include <thread>

#include "absl/status/status.h"

namespace config {
class Config;
}

namespace joshua {

// One runtime per process; configure it before constructing drivers or workers.
void JoshuaInit();
void JoshuaInit(const config::Config& configuration);
void ThrowIfError(const absl::Status& status);

bool ShutdownRequested() noexcept;
bool CancellationRequested() noexcept;
bool RuntimeFailed() noexcept;
void RequestShutdown() noexcept;
void ReportFailure(const char* context, std::exception_ptr error) noexcept;

// Nested runners reuse the outer boundary. Diagnostics do not read RobotTime.
int RunGuarded(const char* name, const std::function<int()>& application);

namespace internal {
bool SetCleanupActive(bool active) noexcept;
class CleanupScope {
 public:
  explicit CleanupScope(bool active = true) noexcept : previous_(SetCleanupActive(active)) {}
  ~CleanupScope() { SetCleanupActive(previous_); }

 private:
  bool previous_;
};
}  // namespace internal

template <typename Cleanup>
bool RunCleanup(const char* name, Cleanup&& cleanup) noexcept {
  internal::CleanupScope scope;
  try {
    cleanup();
    return true;
  } catch (...) {
    ReportFailure(name, std::current_exception());
    return false;
  }
}

std::thread StartWorker(std::string name, std::function<void()> work);

// Called after construction, before spinning. A direct launch has no ready fd.
absl::Status NotifyReady();

}  // namespace joshua

using joshua::JoshuaInit;

#define JOSHUA_MAIN(argc_name, argv_name)                                     \
  static int JoshuaMainBody(int argc_name, char** argv_name);                 \
  int main(int argc_name, char** argv_name) {                                 \
    return ::joshua::RunGuarded(argv_name[0], [&] {                            \
      return JoshuaMainBody(argc_name, argv_name);                            \
    });                                                                     \
  }                                                                         \
  static int JoshuaMainBody(int argc_name, char** argv_name)
