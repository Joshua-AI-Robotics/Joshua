#include "utils/joshua_runtime.h"

#include <unistd.h>

#include <stdexcept>
#include <memory>

#include "gtest/gtest.h"
#include "utils/robot_time.h"
#include "utils/scope_exit.h"

namespace joshua {
namespace {

TEST(JoshuaRuntimeTest, UnwindsResourcesAndReportsMainFailure) {
  bool released = false;
  const int result = RunGuarded("application", [&]() -> int {
    ScopeExit cleanup([&] { released = true; });
    throw std::runtime_error("clock unavailable");
  });
  EXPECT_EQ(result, 1);
  EXPECT_TRUE(released);
  EXPECT_TRUE(RuntimeFailed());
  EXPECT_TRUE(ShutdownRequested());
}

TEST(JoshuaRuntimeTest, NestedRunnerPropagatesToTheOuterBoundary) {
  bool reached_inner_caller = false;
  const int result = RunGuarded("outer", [&] {
    RunGuarded("inner", []() -> int { throw std::logic_error("construction failure"); });
    reached_inner_caller = true;
    return 0;
  });
  EXPECT_EQ(result, 1);
  EXPECT_FALSE(reached_inner_caller);
}

TEST(JoshuaRuntimeTest, WorkerFailureRequestsOwnerShutdown) {
  const int result = RunGuarded("owner", [] {
    auto worker = StartWorker("capture", [] { throw std::runtime_error("worker failed"); });
    worker.join();
    EXPECT_TRUE(ShutdownRequested());
    return 0;
  });
  EXPECT_EQ(result, 1);
}

TEST(JoshuaRuntimeTest, CleanupCanAttemptStopCommandsAfterCancellation) {
  EXPECT_EQ(RunGuarded("owner", [] {
    RequestShutdown();
    EXPECT_TRUE(CancellationRequested());
    EXPECT_TRUE(RunCleanup("device stop", [] {
      EXPECT_FALSE(CancellationRequested());
      auto worker = StartWorker("stop worker", [] { EXPECT_FALSE(CancellationRequested()); });
      worker.join();
    }));
    EXPECT_TRUE(CancellationRequested());
    return 0;
  }), 0);
}

TEST(JoshuaRuntimeTest, CleanupFailureCannotTurnFailureIntoSuccess) {
  EXPECT_EQ(RunGuarded("owner", [] {
    RunCleanup("close", [] { throw std::runtime_error("close failed"); });
    return 0;
  }), 1);
  EXPECT_EQ(RunGuarded("exit code", [] { return 7; }), 7);
}

class InvalidClock final : public RobotClock {
 public:
  double Now() const override { throw std::runtime_error("device lost"); }
};

TEST(JoshuaRuntimeTest, ClockExceptionsHaveACommonType) {
  ASSERT_EXIT({
    if (!SetGlobalRobotClock(std::make_unique<InvalidClock>()).ok()) _exit(2);
    try {
      RobotTime();
    } catch (const RobotTimeError&) {
      _exit(0);
    }
    _exit(3);
  }, testing::ExitedWithCode(0), "");
}

}  // namespace
}  // namespace joshua
