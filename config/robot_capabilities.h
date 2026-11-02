#pragma once

#include "config/proto/config.pb.h"
#include "google/protobuf/struct.pb.h"

namespace config {
// Pure description of a validated candidate. Never constructs runtime objects.
// The caller owns integrity validation and encoded output bounds.
google::protobuf::Value BuildRobotCapabilities(const Config& config);
}  // namespace config
