#include "ros2/utils/robot_time_conversion.h"

#include "gtest/gtest.h"

namespace ros2_utils {
namespace {
TEST(RobotTimeConversionTest, PreservesExactPositiveZeroAndNegativeStamps) {
  constexpr int64_t kMinRos =
      static_cast<int64_t>(std::numeric_limits<int32_t>::min()) * 1000000000;
  constexpr int64_t kMaxRos =
      static_cast<int64_t>(std::numeric_limits<int32_t>::max()) * 1000000000 + 999999999;
  for (int64_t ns : {int64_t{0},
                     int64_t{-1},
                     int64_t{-1700000001},
                     int64_t{1700000000123456789},
                     kMinRos,
                     kMaxRos}) {
    auto stamp = ToRosTimestamp(ns);
    ASSERT_TRUE(stamp.ok()) << stamp.status();
    EXPECT_LT(stamp->nanosec, 1000000000u);
    auto result = FromRosTimestamp(*stamp);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, ns);
  }
  auto negative = ToRosTimestamp(-1);
  ASSERT_TRUE(negative.ok());
  EXPECT_EQ(negative->sec, -1);
  EXPECT_EQ(negative->nanosec, 999999999u);
  EXPECT_FALSE(ToRosTimestamp(kMinRos - 1).ok());
  EXPECT_FALSE(ToRosTimestamp(kMaxRos + 1).ok());
  EXPECT_FALSE(ToRosTimestamp(std::numeric_limits<int64_t>::min()).ok());
  EXPECT_FALSE(ToRosTimestamp(std::numeric_limits<int64_t>::max()).ok());
}

TEST(RobotTimeConversionTest, RejectsInvalidRosNanoseconds) {
  builtin_interfaces::msg::Time stamp;
  stamp.nanosec = 1000000000;
  EXPECT_EQ(FromRosTimestamp(stamp).status().code(), absl::StatusCode::kInvalidArgument);
}
}  // namespace
}  // namespace ros2_utils
