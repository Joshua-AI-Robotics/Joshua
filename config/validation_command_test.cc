#include "config/validation_command.h"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "config/config_utils.h"
#include "gtest/gtest.h"

namespace config {
namespace {
class ValidationCommandTest : public ::testing::Test {
 protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            ("joshua-validation-test-" + std::to_string(getpid()));
    std::filesystem::create_directories(path_);
  }
  void TearDown() override {
    std::filesystem::remove_all(path_);
  }
  std::filesystem::path path_;
};

TEST_F(ValidationCommandTest, BoundedLoadAcceptsExactLimitAndRejectsNextByte) {
  auto file = path_ / "config.pbtxt";
  std::ofstream(file) << "   ";
  EXPECT_TRUE(config_util::LoadConfig(file.string(), 3).ok());
  EXPECT_EQ(config_util::LoadConfig(file.string(), 2).status().code(),
            absl::StatusCode::kResourceExhausted);
}

TEST_F(ValidationCommandTest, BoundedLoadHandlesZeroLimit) {
  auto file = path_ / "config.pbtxt";
  std::ofstream(file).close();
  EXPECT_TRUE(config_util::LoadConfig(file.string(), 0).ok());
  std::ofstream(file) << " ";
  EXPECT_EQ(config_util::LoadConfig(file.string(), 0).status().code(),
            absl::StatusCode::kResourceExhausted);
}

TEST_F(ValidationCommandTest, MaximumByteLimitStillReadsToEof) {
  auto file = path_ / "config.pbtxt";
  std::ofstream(file) << " ";
  EXPECT_TRUE(config_util::LoadConfig(file.string(), std::numeric_limits<size_t>::max()).ok());
}

TEST_F(ValidationCommandTest, EmptyConfigPreservesCurrentValidatorSemantics) {
  auto file = path_ / "empty.pbtxt";
  std::ofstream(file).close();
  std::ostringstream output;
  EXPECT_EQ(RunValidationCommand({"--config", file.string()}, output), 0);
  EXPECT_NE(output.str().find("\"result\":\"valid\""), std::string::npos);
}

TEST_F(ValidationCommandTest, PinnedObjectSurvivesPathReplacement) {
  auto file = path_ / "config.pbtxt";
  std::ofstream(file) << " ";
  const int pinned = open(file.c_str(), O_PATH | O_CLOEXEC);
  ASSERT_GE(pinned, 0);
  std::filesystem::rename(file, path_ / "original.pbtxt");
  std::ofstream(file) << "unknown_field: 1";
  auto original = config_util::LoadConfig("/proc/self/fd/" + std::to_string(pinned), 1024);
  auto replacement = config_util::LoadConfig(file.string(), 1024);
  close(pinned);
  EXPECT_TRUE(original.ok());
  EXPECT_EQ(replacement.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST_F(ValidationCommandTest, DetectsOutputFailure) {
  auto file = path_ / "empty.pbtxt";
  std::ofstream(file).close();
  std::ostringstream output;
  output.setstate(std::ios::badbit);
  EXPECT_EQ(RunValidationCommand({"--config", file.string()}, output), 3);
}
}  // namespace
}  // namespace config
