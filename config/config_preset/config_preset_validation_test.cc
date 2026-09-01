#include <filesystem>
#include <string>
#include <vector>

#include "config/config_utils.h"
#include "gtest/gtest.h"
#include "robot/board/factory/sensor_channel_validation.h"

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

// Static counterpart to PerceptionFactory::CreateBoardEncoder: every
// ENCODER in every preset must resolve to a real board channel. This is
// the check that was missing when commit 8cf099a moved the so100 presets
// onto boards{} -- it stripped the encoders' inline comm without giving
// them a board_name, and nothing failed until the node was launched
// against hardware.
TEST(ConfigValidationTest, EveryEncoderResolvesToABoardChannel) {
  const std::string directory = kConfigPresetDirectory;
  ASSERT_TRUE(fs::exists(directory)) << "Config directory not found: " << directory;

  for (const auto& entry : fs::recursive_directory_iterator(directory)) {
    if (entry.path().extension() != ".pbtxt") {
      continue;
    }
    const std::string config_path = entry.path().string();
    auto config = config::config_util::LoadConfig(config_path);
    ASSERT_TRUE(config.ok()) << config_path << ": " << config.status().message();

    for (const auto& single_perception : config->robot().perceptions().single_perceptions()) {
      if (single_perception.perception_type() != robot::perception::PerceptionType::ENCODER) {
        continue;
      }
      const auto& encoder = single_perception.encoder();
      const std::string where = config_path + ": encoder '" + encoder.encoder_name() + "'";

      EXPECT_FALSE(encoder.has_comm())
          << where
          << " still sets the deprecated inline comm; declare the port once in "
             "boards{} and set board_name + channel.";
      ASSERT_FALSE(encoder.board_name().empty()) << where << " has no board_name.";

      const robot::board::Board* board = nullptr;
      for (const auto& candidate : config->robot().boards()) {
        if (candidate.name() == encoder.board_name()) {
          board = &candidate;
          break;
        }
      }
      ASSERT_NE(board, nullptr) << where << " references board '" << encoder.board_name()
                                << "' but no boards{} entry declares that name.";

      const robot::board::Channel* channel = nullptr;
      for (const auto& candidate : board->channels()) {
        if (candidate.index() == encoder.channel()) {
          channel = &candidate;
          break;
        }
      }
      ASSERT_NE(channel, nullptr) << where << " uses channel " << encoder.channel()
                                  << " but board '" << board->name() << "' does not declare it.";

      const auto status =
          robot::board::ValidateSensorChannel(encoder.encoder_type(), channel->drive());
      EXPECT_TRUE(status.ok()) << where << ": " << status.message();
    }
  }
}
}  // namespace
