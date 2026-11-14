#include "robot/board/host_gpio/resource_leases.h"

#include <unistd.h>

#include <cstdlib>
#include <filesystem>

#include "gtest/gtest.h"
namespace robot::board {
TEST(ResourceLeasesTest, ExclusiveClaimsAndPartialFailureReleaseEarlierLocks) {
  char path[] = "/tmp/joshua-lease-XXXXXX";
  ASSERT_NE(mkdtemp(path), nullptr);
  {
    auto first = ResourceLeases::Acquire({"z"}, path);
    ASSERT_TRUE(first.ok());
    EXPECT_FALSE(ResourceLeases::Acquire({"a", "z"}, path).ok());
    auto earlier = ResourceLeases::Acquire({"a"}, path);
    EXPECT_TRUE(earlier.ok());
    EXPECT_FALSE(ResourceLeases::Acquire({"z"}, path).ok());
    first->reset();
    EXPECT_TRUE(ResourceLeases::Acquire({"z"}, path).ok());
  }
  std::filesystem::remove_all(path);
}
TEST(ResourceLeasesTest, SymlinkDirectoriesAreRejected) {
  char path[] = "/tmp/joshua-lease-symlink-XXXXXX";
  ASSERT_NE(mkdtemp(path), nullptr);
  const auto dir = std::filesystem::path(path);
  std::filesystem::create_directory(dir / "target");
  std::filesystem::create_directory_symlink(dir / "target",
                                            dir / ("uid-" + std::to_string(geteuid())));
  EXPECT_FALSE(ResourceLeases::Acquire({"pin"}, path).ok());
  std::filesystem::remove_all(path);
}
}  // namespace robot::board
