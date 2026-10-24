#include "ros2/utils/robot_time_conversion.h"

#include "gtest/gtest.h"

namespace ros2_utils {
namespace {
TEST(RobotTimeConversionTest, ConvertsDoubleSecondsAndNormalizesNegativeTimes) {
  constexpr double kMinRos = std::numeric_limits<int32_t>::min();
  constexpr double kUpperExclusive = static_cast<double>(std::numeric_limits<int32_t>::max()) + 1.0;
  const double max_safe = std::nextafter(kUpperExclusive, 0.0);
  for (double seconds :
       {0.0, -0.5, -0.000000001, -1.000000001, 123.000000456, 1700000000.125, kMinRos, max_safe}) {
    auto stamp = ToRosTimestamp(seconds);
    ASSERT_TRUE(stamp.ok()) << stamp.status();
    EXPECT_LT(stamp->nanosec, 1000000000u);
    auto result = FromRosTimestamp(*stamp);
    ASSERT_TRUE(result.ok());
    EXPECT_DOUBLE_EQ(*result, seconds);
  }
  auto negative = ToRosTimestamp(-0.000000001);
  ASSERT_TRUE(negative.ok());
  EXPECT_EQ(negative->sec, -1);
  EXPECT_EQ(negative->nanosec, 999999999u);
  EXPECT_FALSE(
      ToRosTimestamp(std::nextafter(kMinRos, -std::numeric_limits<double>::infinity())).ok());
  EXPECT_FALSE(ToRosTimestamp(kUpperExclusive).ok());
  EXPECT_FALSE(ToRosTimestamp(std::numeric_limits<double>::max()).ok());
}

TEST(RobotTimeConversionTest, RoundsNanosecondsWithCarryAndRejectsNonFiniteSeconds) {
  auto rounded = ToRosTimestamp(1.9999999996);
  ASSERT_TRUE(rounded.ok());
  EXPECT_EQ(rounded->sec, 2);
  EXPECT_EQ(rounded->nanosec, 0u);
  for (double value : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
    EXPECT_EQ(ToRosTimestamp(value).status().code(), absl::StatusCode::kInvalidArgument);
  }
}

TEST(RobotTimeConversionTest, RejectsInvalidRosNanoseconds) {
  builtin_interfaces::msg::Time stamp;
  stamp.nanosec = 1000000000;
  EXPECT_EQ(FromRosTimestamp(stamp).status().code(), absl::StatusCode::kInvalidArgument);
}
}  // namespace
}  // namespace ros2_utils
