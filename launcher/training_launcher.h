#pragma once

#include <string>

#include "config/proto/config.pb.h"

namespace launcher {

// Launches the AI trainer binary (//ai/train:trainer) as a child process.
// Returns the process exit code (0 on success).
int RunTraining(const std::string& config_path, const config::Config& config);

}  // namespace launcher
