#pragma once

#include "absl/status/status.h"
#include "config/proto/robot.pb.h"

namespace config::config_util {
// Checks sensor configs and serial-port ownership across actuator and perception nodes.
// Does not open devices or instantiate drivers.
absl::Status ValidatePerceptions(const Robot& robot);
}  // namespace config::config_util
