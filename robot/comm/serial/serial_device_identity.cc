#include "robot/comm/serial/serial_device_identity.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <system_error>

#include "absl/status/status.h"

namespace robot::comm {

absl::StatusOr<SerialDeviceIdentity> ResolveSerialDeviceIdentity(const std::string& port) {
  if (port.empty() || port.find('\0') != std::string::npos)
    return absl::InvalidArgumentError("Serial port must be a nonempty path without NUL bytes.");
  std::error_code error;
  const auto absolute = std::filesystem::absolute(port, error);
  if (error)
    return absl::UnavailableError("Cannot resolve serial port '" + port + "': " + error.message());
  const auto path = std::filesystem::weakly_canonical(absolute, error);
  if (error)
    return absl::UnavailableError("Cannot resolve serial port '" + port + "': " + error.message());

  struct stat device {};
  if (stat(path.c_str(), &device) == 0) {
    // st_rdev identifies the same character device even through different device
    // nodes or mount aliases. Other files use filesystem/inode identity.
    const auto key = S_ISCHR(device.st_mode)
                         ? "character:" + std::to_string(static_cast<uintmax_t>(device.st_rdev))
                         : "inode:" + std::to_string(static_cast<uintmax_t>(device.st_dev)) + ":" +
                               std::to_string(static_cast<uintmax_t>(device.st_ino));
    return SerialDeviceIdentity{path.string(), key};
  }
  const int stat_error = errno;
  if (stat_error != ENOENT)
    return absl::UnavailableError("Cannot identify serial port '" + port + "': " +
                                  std::error_code(stat_error, std::generic_category()).message());
  return SerialDeviceIdentity{path.string(), "path:" + path.string()};
}

}  // namespace robot::comm
