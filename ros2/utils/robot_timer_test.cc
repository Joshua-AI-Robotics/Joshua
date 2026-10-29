#include "ros2/utils/robot_timer.h"

#include <limits>
#include <memory>
#include <stdexcept>

#include "gtest/gtest.h"

namespace ros2_utils {
namespace {

class TimerClock final : public joshua::RobotClock {
 public:
  double Now() const override {
    if (fail) throw std::runtime_error("test clock unavailable");
    return now;
  }
  double now = 42.0;
  bool fail = false;
};

class RobotTimerTest : public testing::Test {
 protected:
  static void SetUpTestSuite() {
    auto clock = std::make_unique<TimerClock>();
    clock_ = clock.get();
    ASSERT_TRUE(joshua::SetGlobalRobotClock(std::move(clock)).ok());
  }
  void SetUp() override {
    clock_->now = 42.0;
    clock_->fail = false;
  }
  inline static TimerClock* clock_ = nullptr;
};

TEST_F(RobotTimerTest, WakeupsDoNotFireBeforeRobotTimeDeadline) {
  int calls = 0;
  auto tick = internal::MakeRobotTimerCallback(0.25, [&] { ++calls; });
  for (int i = 0; i < 100; ++i) tick();
  EXPECT_EQ(calls, 0);
  clock_->now = 42.25;
  for (int i = 0; i < 100; ++i) tick();
  EXPECT_EQ(calls, 1);
  clock_->now = 42.5;
  tick();
  EXPECT_EQ(calls, 2);
}

TEST_F(RobotTimerTest, SmallWakeupDelaysPreserveCadence) {
  int calls = 0;
  auto tick = internal::MakeRobotTimerCallback(0.25, [&] { ++calls; });
  clock_->now = 42.26;
  tick();
  EXPECT_EQ(calls, 1);
  clock_->now = 42.5;
  tick();
  EXPECT_EQ(calls, 2);
}

TEST_F(RobotTimerTest, ForwardJumpSkipsMissedPeriodsWithoutBurst) {
  int calls = 0;
  auto tick = internal::MakeRobotTimerCallback(0.25, [&] { ++calls; });
  clock_->now = 44.125;
  for (int i = 0; i < 100; ++i) tick();
  EXPECT_EQ(calls, 1);
  clock_->now = 44.375;
  tick();
  EXPECT_EQ(calls, 2);
}

TEST_F(RobotTimerTest, BackwardJumpWaitsForExistingDeadline) {
  int calls = 0;
  auto tick = internal::MakeRobotTimerCallback(0.25, [&] { ++calls; });
  clock_->now = 41.0;
  tick();
  EXPECT_EQ(calls, 0);
  clock_->now = 42.25;
  tick();
  EXPECT_EQ(calls, 1);
  clock_->now = 40.0;
  tick();
  EXPECT_EQ(calls, 1);
  clock_->now = 42.5;
  tick();
  EXPECT_EQ(calls, 2);
}

TEST_F(RobotTimerTest, TimersKeepIndependentPeriods) {
  int fast_calls = 0;
  int slow_calls = 0;
  auto fast = internal::MakeRobotTimerCallback(0.25, [&] { ++fast_calls; });
  auto slow = internal::MakeRobotTimerCallback(0.5, [&] { ++slow_calls; });
  clock_->now = 42.25;
  fast();
  slow();
  EXPECT_EQ(fast_calls, 1);
  EXPECT_EQ(slow_calls, 0);
  clock_->now = 42.5;
  fast();
  slow();
  EXPECT_EQ(fast_calls, 2);
  EXPECT_EQ(slow_calls, 1);
}

TEST_F(RobotTimerTest, RejectsInvalidPeriodsAndEmptyCallbacks) {
  for (double period : {0.0,
                        -1.0,
                        1e-10,
                        std::numeric_limits<double>::quiet_NaN(),
                        std::numeric_limits<double>::infinity()}) {
    EXPECT_THROW(internal::MakeRobotTimerCallback(period, [] {}), std::invalid_argument);
  }
  EXPECT_THROW(internal::MakeRobotTimerCallback(1.0, {}), std::invalid_argument);
  clock_->now = 1'700'000'000.0;
  EXPECT_THROW(internal::MakeRobotTimerCallback(1e-9, [] {}), std::invalid_argument);
}

TEST_F(RobotTimerTest, ClockFailuresPropagateAtCreationAndWakeup) {
  clock_->fail = true;
  EXPECT_THROW(internal::MakeRobotTimerCallback(0.25, [] {}), std::runtime_error);
  clock_->fail = false;
  auto tick = internal::MakeRobotTimerCallback(0.25, [] {});
  clock_->fail = true;
  EXPECT_THROW(tick(), std::runtime_error);
  clock_->fail = false;
  clock_->now = std::numeric_limits<double>::quiet_NaN();
  EXPECT_THROW(tick(), std::runtime_error);
}

}  // namespace
}  // namespace ros2_utils
