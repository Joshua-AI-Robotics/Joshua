#pragma once

#include <string>

#include "absl/status/statusor.h"

namespace robot::comm {

struct SerialDeviceIdentity {
  std::string path;  // Absolute path with existing symlinks resolved, used for opening.
  std::string key;   // Character-device number, or normalized path if it is absent.
};

// Reads filesystem metadata only; never opens the device. Missing devices retain
// a normalized path identity so offline config validation remains possible.
absl::StatusOr<SerialDeviceIdentity> ResolveSerialDeviceIdentity(const std::string& port);

}  // namespace robot::comm
