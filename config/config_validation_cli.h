#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "absl/status/statusor.h"
#include "config/proto/config.pb.h"

namespace config {

// Shared bounded, nofollow descriptor-relative load used by read-only inspection.
// stage distinguishes input failures from protobuf parser failures.
absl::StatusOr<Config> LoadConfigForInspection(const std::string& path,
                                               int root_fd,
                                               std::string* stage);

// Internal command boundary. Returns the process exit code and writes its
// single JSON report to output (or plain text for --help).
int RunConfigValidationCli(const std::vector<std::string>& args, std::ostream& output);

}  // namespace config
