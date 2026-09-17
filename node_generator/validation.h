#pragma once

#include "absl/status/status.h"
#include "config/proto/config.pb.h"

namespace node_generator {

// Config-wide entry point for integrity checks before launching nodes. Composes
// resource, node-assignment, serial-setting, and bus-ownership checks; additional
// config sections can add their checks here. Does not open hardware or create
// drivers. Driver-specific requirements remain with their respective factories.
absl::Status ValidateConfig(const config::Config& config);

}  // namespace node_generator
