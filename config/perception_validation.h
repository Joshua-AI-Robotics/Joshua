#pragma once

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"

namespace config::config_util {
// Checks declared resource dependencies, node assignments, serial settings,
// and bus ownership across actuator and perception nodes. Driver-specific
// requirements belong to the perception factory; no sensor/publisher allowlist.
// Does not open devices or instantiate drivers.
absl::Status ValidatePerceptions(const Robot& robot);
}  // namespace config::config_util
