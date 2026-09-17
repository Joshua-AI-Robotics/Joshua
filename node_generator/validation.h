#pragma once

#include "absl/status/status.h"
#include "config/proto/config.pb.h"

namespace node_generator {

// Config-wide entry point for integrity checks before launching nodes. Composes
// resource, node-assignment, serial-setting, and bus-ownership checks; additional
// config sections can add their checks here. Does not open hardware or create
// drivers. Sensor config checks and dependency extraction are private to this
// module; factories retain defensive checks at construction boundaries.
absl::Status ValidateConfig(const config::Config& config);

}  // namespace node_generator
