#pragma once
#include <string>

#include "absl/status/statusor.h"

namespace joshua::mcp {
// Expected ROS endpoint from a candidate config, never a claim about the running launcher.
struct RosSourceBinding {
  std::string camera, topic, node, ns;
  int domain;
  bool operator==(const RosSourceBinding& other) const;
};
absl::StatusOr<RosSourceBinding> ResolveImageSource(int root_fd,
                                                    const std::string& config_path,
                                                    const std::string& source_id,
                                                    int ros_domain);
}  // namespace joshua::mcp
