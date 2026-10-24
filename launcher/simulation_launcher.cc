#include "launcher/simulation_launcher.h"

#include <glog/logging.h>
#include <limits.h>
#include <poll.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "node_generator/node_generator.h"
#include "node_generator/process_supervisor.h"
#include "utils/joshua_runtime.h"
#include "utils/scope_exit.h"

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

node_generator::ProcessSpec BuildSimulationSpec(const std::filesystem::path& sim_bin, const std::string& config_path) {
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

  node_generator::ProcessSpec spec;
  spec.name = "simulation";
  spec.executable = exec_path;
  spec.arguments = {"--config", abs_config};
  spec.environment = std::move(env_set);
  spec.unset_environment = std::move(env_unset);
  spec.working_directory = std::move(work_dir);
  spec.require_ready = false;
  spec.allow_completion = true;
  return spec;
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
    const auto launched = ng->LaunchAllNodes();
    if (absl::IsCancelled(launched) && node_generator::SupervisorShutdownRequested()) return 0;
    joshua::ThrowIfError(launched);
  }

  node_generator::ProcessSupervisor simulation;
  joshua::ScopeExit cleanup([&] {
    if (ng) joshua::RunCleanup("simulation graph cleanup", [&] { joshua::ThrowIfError(ng->Shutdown()); });
    joshua::RunCleanup("simulation cleanup", [&] { joshua::ThrowIfError(simulation.Shutdown()); });
  });
  const auto started = simulation.Start(BuildSimulationSpec(*sim_bin, config_path));
  if (absl::IsCancelled(started) && node_generator::SupervisorShutdownRequested()) return 0;
  joshua::ThrowIfError(started);
  const auto ready = simulation.AwaitReady();
  if (absl::IsCancelled(ready) && node_generator::SupervisorShutdownRequested()) return 0;
  joshua::ThrowIfError(ready);
  while (!node_generator::SupervisorShutdownRequested() && !joshua::ShutdownRequested()) {
    if (ng) joshua::ThrowIfError(ng->PollNodes());
    joshua::ThrowIfError(simulation.Poll());
    if (!simulation.HasLiveProcesses()) break;
    poll(nullptr, 0, 20);
  }
  return joshua::RuntimeFailed() ? 1 : 0;
}

}  // namespace launcher
