#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "robot/comm/proto/comm.pb.h"
#include "robot/perception/proto/perception.pb.h"

namespace robot::perception {

struct BoardChannelReference {
  std::string board_name;
  uint32_t channel;
};

// Resources declared by a concrete sensor config, independent of what it
// measures. Drivers may use board channels, direct comms, both, or neither.
struct SensorDependencies {
  std::vector<BoardChannelReference> board_channels;
  std::vector<robot::comm::Comm> comms;
};

// Checks driver-specific config requirements and describes its dependencies.
// New drivers declare their resources here; shared config validation does not
// need sensor-type or driver-specific cases. Does not resolve or open resources.
absl::StatusOr<SensorDependencies> GetSensorDependencies(const SinglePerception& sensor);

// Driver-specific validation for factory callers; resource resolution remains
// with the factory and shared configuration checks.
absl::Status ValidateSensorConfig(const SinglePerception& sensor);

}  // namespace robot::perception
