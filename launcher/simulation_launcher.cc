#include "launcher/simulation_launcher.h"

#include <glog/logging.h>
#include <limits.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "node_generator/node_generator.h"

namespace launcher {
namespace {

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

std::optional<std::filesystem::path> ResolveSimulationBinary() {
  std::string self = GetSelfExePath();
  if (self.empty()) return std::nullopt;

  std::filesystem::path exe_dir = std::filesystem::path(self).parent_path();
  if (!exe_dir.has_parent_path()) return std::nullopt;

  std::filesystem::path bazel_bin_root = exe_dir.parent_path();
  std::filesystem::path candidate = bazel_bin_root / "simulation" / "simulation";
  if (std::filesystem::exists(candidate) && access(candidate.c_str(), X_OK) == 0) {
    return candidate;
  }

  if (auto runfiles = GetRunfilesRoot()) {
    for (const char* ws : {"_main", "__main__"}) {
      std::filesystem::path c = *runfiles / ws / "simulation" / "simulation";
      if (std::filesystem::exists(c) && access(c.c_str(), X_OK) == 0) {
        return c;
      }
    }
  }

  return std::nullopt;
}

pid_t ForkExecSimulation(const std::filesystem::path& sim_bin, const std::string& config_path) {
  std::string exec_path = sim_bin.string();

  std::filesystem::path runfiles_dir =
      sim_bin.parent_path() / (sim_bin.filename().string() + ".runfiles");

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
    const char* msg = "Failed to execute simulation binary\n";
    write(STDERR_FILENO, msg, strlen(msg));
    _exit(1);
  }
  return pid;
}

bool HasPerceptionNodes(const config::Config& config) {
  return config.robot().perceptions().single_perceptions_size() > 0;
}

}  // namespace

int RunSimulation(const std::string& config_path, const config::Config& config) {
  auto sim_bin = ResolveSimulationBinary();
  if (!sim_bin) {
    LOG(ERROR) << "Could not locate simulation/simulation binary. "
               << "Make sure //simulation:simulation is built.";
    return 1;
  }
  LOG(INFO) << "Resolved simulation binary: " << sim_bin->string();

  std::unique_ptr<node_generator::NodeGenerator> ng;

  if (HasPerceptionNodes(config)) {
    LOG(INFO) << "Config has robot perceptions -- launching encoder publishers "
              << "for mirror mode via NodeGenerator";
    ng = std::make_unique<node_generator::NodeGenerator>(config_path);
    if (!ng->Initialize().ok()) {
      LOG(ERROR) << "Failed to initialize NodeGenerator for perception nodes";
      return 1;
    }
    if (!ng->LaunchAllNodes().ok()) {
      LOG(WARNING) << "NodeGenerator launched no perception nodes";
    }
  }

  pid_t sim_pid = ForkExecSimulation(*sim_bin, config_path);
  if (sim_pid <= 0) {
    LOG(ERROR) << "Failed to fork simulation process";
    if (ng) ng->Shutdown();
    return 1;
  }
  LOG(INFO) << "Simulation launched with PID: " << sim_pid;

  int status = 0;
  waitpid(sim_pid, &status, 0);

  int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 1;
  LOG(INFO) << "Simulation exited with code: " << exit_code;

  if (ng) {
    LOG(INFO) << "Shutting down perception nodes ...";
    ng->Shutdown();
  }

  return exit_code;
}

}  // namespace launcher
