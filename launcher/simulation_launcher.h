#pragma once

#include <string>

#include "config/proto/config.pb.h"

namespace launcher {

// Launches the simulation binary (and optionally perception nodes for mirror
// mode).  Returns the process exit code (0 on success).
int RunSimulation(const std::string& config_path, const config::Config& config);

}  // namespace launcher
