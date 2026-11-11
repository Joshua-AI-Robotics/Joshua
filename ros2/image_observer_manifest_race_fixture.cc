// Test-only hook atomically replaces an already pinned manifest with identical bytes.
#include <fcntl.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ros2/image_observer_backend.h"

namespace {
std::runtime_error Failure(const std::string& operation) {
  return std::runtime_error("manifest race fixture " + operation + ": " + std::strerror(errno));
}
std::string ReadManifest(int directory) {
  int fd = openat(directory, "camera-source.json", O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd < 0) throw Failure("open source");
  std::array<char, 16385> bytes{};
  size_t used = 0;
  while (used < bytes.size()) {
    auto n = read(fd, bytes.data() + used, bytes.size() - used);
    if (n < 0 && errno == EINTR) continue;
    if (n < 0) {
      auto error = Failure("read source");
      close(fd);
      throw error;
    }
    if (n == 0) break;
    used += n;
  }
  if (close(fd)) throw Failure("close source");
  if (used == 0 || used > 16384) throw std::runtime_error("manifest race fixture source size");
  return {bytes.data(), used};
}
void ReplaceManifest(int directory, const std::string& bytes) {
  const char* temporary = ".manifest-read-race";
  int fd = openat(directory, temporary, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
  if (fd < 0) throw Failure("create replacement");
  try {
    size_t used = 0;
    while (used < bytes.size()) {
      auto n = write(fd, bytes.data() + used, bytes.size() - used);
      if (n < 0 && errno == EINTR) continue;
      if (n == 0) errno = EIO;
      if (n <= 0) throw Failure("write replacement");
      used += n;
    }
    int closing = fd;
    fd = -1;
    if (close(closing)) throw Failure("close replacement");
    if (renameat(directory, temporary, directory, "camera-source.json"))
      throw Failure("rename replacement");
  } catch (...) {
    if (fd >= 0 && close(fd)) std::cerr << Failure("cleanup close").what() << std::endl;
    if (unlinkat(directory, temporary, 0) && errno != ENOENT)
      std::cerr << Failure("cleanup unlink").what() << std::endl;
    throw;
  }
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 5) return 2;
  try {
    const int directory = std::stoi(argv[2]);
    return ros2_utils::RunImageObserver(
        argc, argv, {}, [directory]() { ReplaceManifest(directory, ReadManifest(directory)); });
  } catch (const std::exception& error) {
    std::cerr << error.what() << std::endl;
    return 2;
  }
}
