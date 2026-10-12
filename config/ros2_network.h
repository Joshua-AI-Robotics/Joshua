#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "config/proto/config.pb.h"

namespace config {

// Pure validation: does not read the environment, create ROS nodes, or open devices.
absl::Status ValidateRos2Network(const Ros2NetworkConfig& config);

// Prepare before ROS initialization or worker threads. Owns generated files and,
// after Apply(), temporarily owns the process environment. Nested preparation
// reuses an identical inherited profile. Destroy children before their owner.
class Ros2NetworkEnvironment {
 public:
  static absl::StatusOr<std::unique_ptr<Ros2NetworkEnvironment>> Prepare(
      const Ros2NetworkConfig& config,
      const std::string& output_parent = "",
      bool reuse_profile = true);
  ~Ros2NetworkEnvironment();
  Ros2NetworkEnvironment(const Ros2NetworkEnvironment&) = delete;
  Ros2NetworkEnvironment& operator=(const Ros2NetworkEnvironment&) = delete;

  absl::Status Apply();
  const std::map<std::string, std::string>& variables() const {
    return variables_;
  }
  // Export tools transfer file lifetime to the caller; no environment is applied.
  void KeepFiles() {
    directory_.clear();
  }

 private:
  Ros2NetworkEnvironment() = default;
  std::map<std::string, std::string> variables_;
  std::map<std::string, std::optional<std::string>> previous_;
  std::string directory_;
  int owner_pid_ = 0;
};
}  // namespace config
