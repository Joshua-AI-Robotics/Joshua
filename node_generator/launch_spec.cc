#include "node_generator/launch_spec.h"

#include <limits.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <optional>
#include <utility>

#include "glog/logging.h"

namespace node_generator {
namespace {
// Common environment variables.
constexpr auto kROS2NodeWrapper = "ros2_node_wrapper.sh";
constexpr auto kROS2 = "ros2";

std::string NodeTypeToString(const ros2::node::NodeType& type) {
  if (type == ros2::node::NODE_INVALID) {
    return "";
  }
  std::string name = ros2::node::NodeType_Name(type);
  std::transform(
      name.begin(), name.end(), name.begin(), [](unsigned char c) { return std::tolower(c); });
  return name;
}

// Resolve current executable absolute path via /proc/self/exe
std::string GetSelfExecutablePath() {
  char buffer[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
  if (len == -1) {
    return "";
  }
  buffer[len] = '\0';
  return std::string(buffer);
}

// Try to determine the Bazel runfiles root directory for this process
std::optional<std::filesystem::path> GetRunfilesRoot() {
  const char* env_runfiles = std::getenv("RUNFILES_DIR");
  if (env_runfiles && std::filesystem::exists(env_runfiles)) {
    return std::filesystem::path(env_runfiles);
  }

  // Fallback: <self_exe>.runfiles
  std::string self_exe = GetSelfExecutablePath();
  if (!self_exe.empty()) {
    std::filesystem::path candidate = std::filesystem::path(self_exe + ".runfiles");
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return std::nullopt;
}

// Try to find the native binary in Bazel's bin tree: <bazel-bin>/ros2/<name>
std::optional<std::filesystem::path> ResolveNativeBinaryInBazelBin(const std::string& exec_name) {
  std::string self_exe = GetSelfExecutablePath();
  if (self_exe.empty()) return std::nullopt;
  std::filesystem::path exe_dir = std::filesystem::path(self_exe).parent_path();
  if (!exe_dir.has_parent_path()) return std::nullopt;
  std::filesystem::path bazel_bin_root = exe_dir.parent_path();
  std::filesystem::path candidate = bazel_bin_root / kROS2 / exec_name;
  if (std::filesystem::exists(candidate)) {
    return candidate;
  }
  return std::nullopt;
}

// Locate the generic wrapper script produced by //ros2:ros2_node_wrapper.sh
std::optional<std::filesystem::path> ResolveRos2WrapperPath() {
  // 1) Next to the current executable (packaged tar layout)
  std::string self_exe = GetSelfExecutablePath();
  if (!self_exe.empty()) {
    std::filesystem::path self_dir = std::filesystem::path(self_exe).parent_path();
    std::filesystem::path candidate = self_dir / kROS2NodeWrapper;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    // Also check under a ros2/ subdirectory in the same extracted tree
    std::filesystem::path candidate_sub = self_dir / kROS2 / kROS2NodeWrapper;
    if (std::filesystem::exists(candidate_sub)) {
      return candidate_sub;
    }
  }

  // 2) In this process' runfiles
  if (auto runfiles_root_opt = GetRunfilesRoot()) {
    const std::filesystem::path runfiles_root = *runfiles_root_opt;
    const char* workspace_dirs[] = {"_main", "__main__"};
    for (const char* ws : workspace_dirs) {
      std::filesystem::path ws_dir = runfiles_root / ws;
      std::filesystem::path candidate = ws_dir / kROS2 / kROS2NodeWrapper;
      if (std::filesystem::exists(candidate)) {
        return candidate;
      }
    }
  }

  // 3) In bazel-bin/ros2
  if (!self_exe.empty()) {
    std::filesystem::path exe_dir = std::filesystem::path(self_exe).parent_path();
    if (exe_dir.has_parent_path()) {
      std::filesystem::path bazel_bin_root = exe_dir.parent_path();
      std::filesystem::path candidate = bazel_bin_root / kROS2 / kROS2NodeWrapper;
      if (std::filesystem::exists(candidate)) {
        return candidate;
      }
    }
  }
  return std::nullopt;
}

bool IsExecutablePathValid(const std::filesystem::path& candidate) {
  return std::filesystem::exists(candidate) && access(candidate.c_str(), X_OK) == 0;
}

bool IsExecutableAvailable(const std::string& exec_name) {
  if (auto native_bin = ResolveNativeBinaryInBazelBin(exec_name)) {
    if (IsExecutablePathValid(*native_bin)) {
      return true;
    }
  }

  // Packaged layout: check next to wrapper or under ros2/
  if (auto wrapper = ResolveRos2WrapperPath()) {
    std::filesystem::path wrapper_dir = wrapper->parent_path();
    std::filesystem::path candidate = wrapper_dir / exec_name;
    if (IsExecutablePathValid(candidate)) {
      return true;
    }
    std::filesystem::path candidate_sub = wrapper_dir / kROS2 / exec_name;
    if (IsExecutablePathValid(candidate_sub)) {
      return true;
    }
  }

  // Runfiles layout
  if (auto runfiles_root_opt = GetRunfilesRoot()) {
    const std::filesystem::path runfiles_root = *runfiles_root_opt;
    const char* workspace_dirs[] = {"_main", "__main__"};
    for (const char* ws : workspace_dirs) {
      std::filesystem::path candidate = runfiles_root / ws / kROS2 / exec_name;
      if (IsExecutablePathValid(candidate)) {
        return true;
      }
    }
  }

  return false;
}

}  // namespace

std::string ResolveConfigPath(const std::string& path) {
  const auto absolute = std::filesystem::absolute(path);
  if (std::filesystem::exists(absolute)) return absolute.lexically_normal().string();
  const auto executable = GetSelfExecutablePath();
  if (!executable.empty()) {
    const auto packaged = std::filesystem::path(executable).parent_path() / path;
    if (std::filesystem::exists(packaged)) return packaged.lexically_normal().string();
  }
  return absolute.lexically_normal().string();
}

absl::StatusOr<ProcessSpec> BuildNodeProcessSpec(ros2::node::NodeType node_type,
                                               uint32_t node_id,
                                               const std::string& node_name,
                                               const std::string& config_path) {
  const std::string node_type_str = NodeTypeToString(node_type);
  if (node_type_str.empty()) {
    LOG(ERROR) << "Invalid node type for node " << node_id;
    return absl::NotFoundError("Cannot resolve executable for " + node_name);
  }

  // The executable is named for the node type, C++ and Python alike: the
  // hardware-facing nodes are C++, and INFERENCE, DATA_SUBSCRIBER, and
  // TRAJECTORY_PUBLISHER are Python with no C++ counterpart to disambiguate
  // from. Nothing is selected between, so nothing is suffixed.
  const std::string& exec_name = node_type_str;

  if (!IsExecutableAvailable(exec_name)) {
    LOG(ERROR) << "No executable available for node '" << node_name << "' (type '" << node_type_str
               << "').";
    return absl::NotFoundError("Cannot resolve executable for " + node_name);
  }

  LOG(INFO) << "Launching node '" << node_name << "' (exec '" << exec_name << "').";

  std::string exec_path;
  std::vector<std::string> argv_strings;
  std::vector<std::pair<std::string, std::string>> env_to_set;
  std::vector<std::string> env_to_unset;
  std::string work_dir;

  // 1) Try to execute the native binary with its own runfiles when available (Bazel run).
  if (auto native_bin = ResolveNativeBinaryInBazelBin(exec_name)) {
    exec_path = native_bin->string();
    if (!std::filesystem::exists(exec_path)) {
      LOG(ERROR) << "Native binary not found at '" << exec_path << "' for node '" << exec_name
                 << "'";
      return absl::NotFoundError("Cannot resolve executable for " + node_name);
    }

    std::filesystem::path bin_path(exec_path);
    std::filesystem::path runfiles_dir =
        bin_path.parent_path() / (bin_path.filename().string() + ".runfiles");

    if (std::filesystem::exists(runfiles_dir)) {
      env_to_set.push_back({"RUNFILES_DIR", runfiles_dir.string()});
      env_to_set.push_back({"TEST_SRCDIR", runfiles_dir.string()});
      env_to_unset = {"RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"};

      std::filesystem::path wd = runfiles_dir / "_main";
      if (std::filesystem::exists(wd) && std::filesystem::is_directory(wd)) {
        work_dir = wd.string();
      }
    } else {
      // If the child binary does not have its own runfiles (e.g. data dependency),
      // try to inherit the parent's runfiles.
      if (auto parent_runfiles = GetRunfilesRoot()) {
        env_to_set.push_back({"RUNFILES_DIR", parent_runfiles->string()});
        env_to_set.push_back({"TEST_SRCDIR", parent_runfiles->string()});
        env_to_unset = {"RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"};

        // Try to set work_dir relative to parent runfiles
        std::filesystem::path wd = *parent_runfiles / "_main";
        if (std::filesystem::exists(wd) && std::filesystem::is_directory(wd)) {
          work_dir = wd.string();
        }
      } else {
        env_to_unset = {"RUNFILES_DIR", "TEST_SRCDIR", "RUNFILES_MANIFEST_FILE", "JAVA_RUNFILES"};
      }
    }

    argv_strings = {exec_path, node_name, std::to_string(node_id), config_path};
  }
  // 2) Try to execute the generic wrapper from packaged tar layout.
  else if (auto wrapper = ResolveRos2WrapperPath()) {
    exec_path = wrapper->string();
    if (!std::filesystem::exists(exec_path)) {
      LOG(ERROR) << "Wrapper script not found at '" << exec_path << "'";
      return absl::NotFoundError("Cannot resolve executable for " + node_name);
    }
    if (access(exec_path.c_str(), X_OK) != 0) {
      LOG(ERROR) << "Wrapper script not executable: '" << exec_path << "' (chmod +x)";
      return absl::NotFoundError("Cannot resolve executable for " + node_name);
    }
    argv_strings = {exec_path, exec_name, node_name, std::to_string(node_id), config_path};
  } else {
    LOG(ERROR) << "Could not locate ROS2 wrapper, native binary, or launch for node type '"
               << exec_name << "'";
    return absl::NotFoundError("Cannot resolve executable for " + node_name);
  }

  ProcessSpec spec;
  spec.name = node_name;
  spec.executable = std::move(exec_path);
  spec.arguments.assign(argv_strings.begin() + 1, argv_strings.end());
  spec.environment = std::move(env_to_set);
  spec.unset_environment = std::move(env_to_unset);
  spec.working_directory = std::move(work_dir);
  // The isolated inference host has not adopted the readiness protocol yet.
  spec.require_ready = node_type != ros2::node::INFERENCE;
  spec.allow_completion = node_type == ros2::node::TRAJECTORY_PUBLISHER;
  return spec;

}
}  // namespace node_generator
