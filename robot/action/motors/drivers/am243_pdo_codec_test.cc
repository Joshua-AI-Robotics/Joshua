#include "robot/action/motors/drivers/am243_pdo_codec.h"

#include <cstdint>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace robot::action::am243 {
namespace {

TEST(Am243PdoCodecTest, EncodeDemoOutputSeedWritesFirstByteOnly) {
  const auto output_pdo = EncodeDemoOutputSeed(0x5a);

  ASSERT_EQ(output_pdo.size(), kDemoPdoSizeBytes);
  EXPECT_EQ(output_pdo[0], 0x5a);
  for (int i = 1; i < kDemoPdoSizeBytes; ++i) {
    EXPECT_EQ(output_pdo[i], 0);
  }
}

TEST(Am243PdoCodecTest, EncodeDemoOutputWalkWritesWalkingBytes) {
  const auto output_pdo = EncodeDemoOutputWalk(0x5a);

  ASSERT_EQ(output_pdo.size(), kDemoPdoSizeBytes);
  for (int i = 0; i < kDemoPdoSizeBytes; ++i) {
    EXPECT_EQ(output_pdo[i], static_cast<uint8_t>(0x5a + i));
  }
}

TEST(Am243PdoCodecTest, DecodeDemoInputEchoSeedReadsFirstByte) {
  std::vector<uint8_t> input_pdo(kDemoPdoSizeBytes, 0);
  input_pdo[0] = 0xa5;

  auto seed_or = DecodeDemoInputEchoSeed(input_pdo);

  ASSERT_TRUE(seed_or.ok()) << seed_or.status();
  EXPECT_EQ(*seed_or, 0xa5);
}

TEST(Am243PdoCodecTest, DecodeDemoInputEchoSeedRejectsWrongSize) {
  auto seed_or = DecodeDemoInputEchoSeed({0x01, 0x02});

  EXPECT_EQ(seed_or.status().code(), absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace robot::action::am243
