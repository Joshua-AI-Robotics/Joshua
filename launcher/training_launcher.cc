#include "launcher/training_launcher.h"

#include <glog/logging.h>
#include <limits.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace launcher {
namespace {

volatile sig_atomic_t g_child_pid = 0;

void ForwardSignalToChild(int sig) {
  pid_t pid = g_child_pid;
  if (pid > 0) {
    kill(-pid, sig);
  }
}

std::string GetSelfExePath() {
  char buf[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
  if (len == -1) return "";
  buf[len] = '\0';
  return std::string(buf);
}

std::optional<std::filesystem::path> GetRunfilesRoot() {
  const char* env = std::getenv("RUNFILES_DIR");
  if (env && std::filesystem::exists(env)) {
    return std::filesystem::path(env);
  }
  std::string self = GetSelfExePath();
  if (!self.empty()) {
    std::filesystem::path candidate(self + ".runfiles");
    if (std::filesystem::exists(candidate)) return candidate;
  }
  return std::nullopt;
}

std::optional<std::filesystem::path> ResolveTrainerBinary() {
  std::string self = GetSelfExePath();
  if (self.empty()) return std::nullopt;

  std::filesystem::path exe_dir = std::filesystem::path(self).parent_path();
  if (!exe_dir.has_parent_path()) return std::nullopt;

  // Look for ai/train/trainer under bazel-bin sibling directory.
  std::filesystem::path bazel_bin_root = exe_dir.parent_path();
  std::filesystem::path candidate = bazel_bin_root / "ai" / "train" / "trainer";
  if (std::filesystem::exists(candidate) && access(candidate.c_str(), X_OK) == 0) {
    return candidate;
  }

  // Fall back to runfiles lookup.
  if (auto runfiles = GetRunfilesRoot()) {
    for (const char* ws : {"_main", "__main__"}) {
      std::filesystem::path c = *runfiles / ws / "ai" / "train" / "trainer";
      if (std::filesystem::exists(c) && access(c.c_str(), X_OK) == 0) {
        return c;
      }
    }
  }

  return std::nullopt;
}

pid_t ForkExecTrainer(const std::filesystem::path& trainer_bin, const std::string& config_path) {
  std::string exec_path = trainer_bin.string();

  std::filesystem::path runfiles_dir =
      trainer_bin.parent_path() / (trainer_bin.filename().string() + ".runfiles");

  std::vector<std::pair<std::string, std::string>> env_set;
  std::vector<std::string> env_unset;
  std::string work_dir;

  if (std::filesystem::exists(runfiles_dir)) {
    env_set.push_back({"RUNFILES_DIR", runfiles_dir.string()});
    env_set.push_back({"TEST_SRCDIR", runfiles_dir.string()});
    env_unset = {"RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"};

    std::filesystem::path wd = runfiles_dir / "_main";
    if (std::filesystem::is_directory(wd)) {
      work_dir = wd.string();
    }
  } else if (auto parent_rf = GetRunfilesRoot()) {
    env_set.push_back({"RUNFILES_DIR", parent_rf->string()});
    env_set.push_back({"TEST_SRCDIR", parent_rf->string()});
    env_unset = {"RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"};

    std::filesystem::path wd = *parent_rf / "_main";
    if (std::filesystem::is_directory(wd)) {
      work_dir = wd.string();
    }
  }

  std::string abs_config = config_path;
  if (!config_path.empty() && config_path[0] != '/') {
    abs_config = std::filesystem::absolute(config_path).string();
  }

  std::vector<std::string> argv_str = {exec_path, "--config", abs_config};
  std::vector<char*> argv_ptrs;
  argv_ptrs.reserve(argv_str.size() + 1);
  for (auto& s : argv_str) argv_ptrs.push_back(const_cast<char*>(s.c_str()));
  argv_ptrs.push_back(nullptr);

  pid_t pid = fork();
  if (pid == 0) {
    setpgid(0, 0);
    for (const auto& [k, v] : env_set) setenv(k.c_str(), v.c_str(), 1);
    for (const auto& k : env_unset) unsetenv(k.c_str());
    if (!work_dir.empty()) chdir(work_dir.c_str());

    execv(exec_path.c_str(), argv_ptrs.data());
    const char* msg = "Failed to execute trainer binary\n";
    write(STDERR_FILENO, msg, strlen(msg));
    _exit(1);
  }
  return pid;
}

}  // namespace

int RunTraining(const std::string& config_path, const config::Config& config) {
  auto trainer_bin = ResolveTrainerBinary();
  if (!trainer_bin) {
    LOG(ERROR) << "Could not locate ai/train/trainer binary. "
               << "Make sure //ai/train:trainer is built.";
    return 1;
  }
  LOG(INFO) << "Resolved trainer binary: " << trainer_bin->string();

  LOG(INFO) << "Launching trainer with config: " << config_path;

  pid_t trainer_pid = ForkExecTrainer(*trainer_bin, config_path);
  if (trainer_pid <= 0) {
    LOG(ERROR) << "Failed to fork trainer process";
    return 1;
  }
  LOG(INFO) << "Trainer launched with PID: " << trainer_pid;

  g_child_pid = trainer_pid;

  struct sigaction sa = {};
  sa.sa_handler = ForwardSignalToChild;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;

  struct sigaction old_int, old_term;
  sigaction(SIGINT, &sa, &old_int);
  sigaction(SIGTERM, &sa, &old_term);

  int status = 0;
  waitpid(trainer_pid, &status, 0);

  sigaction(SIGINT, &old_int, nullptr);
  sigaction(SIGTERM, &old_term, nullptr);
  g_child_pid = 0;

  int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
  LOG(INFO) << "Trainer exited with code: " << exit_code;

  return exit_code;
}

}  // namespace launcher
