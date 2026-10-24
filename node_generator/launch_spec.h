#pragma once

#include <cstdint>
#include <string>

#include "absl/status/statusor.h"
#include "node_generator/process_supervisor.h"
#include "ros2/proto/node.pb.h"

namespace node_generator {

std::string ResolveConfigPath(const std::string& path);

absl::StatusOr<ProcessSpec> BuildNodeProcessSpec(ros2::node::NodeType type,
                                               uint32_t id,
                                               const std::string& name,
                                               const std::string& config_path);

}  // namespace node_generator
