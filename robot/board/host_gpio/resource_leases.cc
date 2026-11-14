#include "robot/board/host_gpio/resource_leases.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <filesystem>
#include <iomanip>
#include <sstream>
namespace robot::board {
ResourceLeases::~ResourceLeases() {
  for (auto fd : descriptors_) close(fd);
}
absl::StatusOr<std::unique_ptr<ResourceLeases>> ResourceLeases::Acquire(
    std::vector<std::string> keys, const std::string& directory) {
  auto result = std::make_unique<ResourceLeases>();
  if (keys.empty()) return result;
  if (directory.empty() || directory.front() != '/')
    return absl::InvalidArgumentError("Resource lease directory must be absolute");
  const auto path = directory + "/uid-" + std::to_string(geteuid());
  std::error_code error;
  std::filesystem::create_directories(path, error);
  if (error) return absl::PermissionDeniedError("Cannot create board lease directory: " + path);
  const int dir = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
  if (dir < 0) return absl::PermissionDeniedError("Invalid board lease directory");
  struct stat info {};
  if (fstat(dir, &info) || info.st_uid != geteuid() || fchmod(dir, 0700)) {
    close(dir);
    return absl::PermissionDeniedError("Board lease directory is not owned by this user");
  }
  std::sort(keys.begin(), keys.end());
  keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  for (const auto& key : keys) {
    uint64_t hash = 14695981039346656037ull;
    for (const unsigned char byte : key) {
      hash ^= byte;
      hash *= 1099511628211ull;
    }
    std::ostringstream name;
    name << std::hex << std::setw(16) << std::setfill('0') << hash << ".lock";
    const int fd = openat(dir, name.str().c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
      close(dir);
      return absl::PermissionDeniedError("Cannot open board resource reservation");
    }
    result->descriptors_.push_back(fd);
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_nlink != 1 ||
        info.st_uid != geteuid() || (info.st_mode & 0077)) {
      close(dir);
      return absl::PermissionDeniedError("Unsafe board resource reservation file");
    }
    if (flock(fd, LOCK_EX | LOCK_NB)) {
      close(dir);
      return absl::AlreadyExistsError("Board pin/mux resource already reserved: " + key);
    }
  }
  close(dir);
  return result;
}
}  // namespace robot::board
