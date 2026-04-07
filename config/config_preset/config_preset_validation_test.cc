#include <filesystem>
#include <string>
#include <vector>

#include "config/config_utils.h"
#include "gtest/gtest.h"

namespace {

namespace fs = std::filesystem;
constexpr auto kConfigPresetDirectory = "config/config_preset";

TEST(ConfigValidationTest, ValidateAllConfigPresets) {
  const std::string directory = kConfigPresetDirectory;

  if (!fs::exists(directory)) {
    FAIL() << "Config directory not found: " << directory;
  }

  int checked_files = 0;
  for (const auto& entry : fs::recursive_directory_iterator(directory)) {
    if (entry.path().extension() == ".pbtxt") {
      const std::string config_path = entry.path().string();
      auto result = config::config_util::LoadConfig(config_path);
      // EXPECT_TRUE continues execution even on failure
      EXPECT_TRUE(result.ok()) << "Failed to load config: " << config_path
                               << "\nError: " << result.status().message();
      checked_files++;
    }
  }
}
}  // namespace
