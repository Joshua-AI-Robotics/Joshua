#include "utils/robot_time.h"

#include <unistd.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "utils/robot_time_internal.h"

double RobotTimeFromHelper();

namespace joshua {
namespace {

class FakeClockIo final : public internal::ClockIo {
 public:
  absl::StatusOr<int> OpenPtp(const std::string& path) override {
    ++opens;
    opened_path = path;
    if (!open_status.ok()) return open_status;
    return 7;
  }
  absl::StatusOr<timespec> Read(clockid_t id) const override {
    last_id = id;
    if (id == CLOCK_REALTIME) {
      ++utc_reads;
      if (!utc_status.ok()) return utc_status;
      return utc_time;
    }
    if (id == CLOCK_MONOTONIC) return timespec{123, 500000000};
    if (!ptp_status.ok()) return ptp_status;
    return ptp_time;
  }
  void Close(int fd) override {
    EXPECT_EQ(fd, 7);
    ++closes;
  }

  int opens = 0;
  int closes = 0;
  mutable int utc_reads = 0;
  mutable clockid_t last_id = 0;
  std::string opened_path;
  absl::Status open_status;
  absl::Status utc_status;
  absl::Status ptp_status;
  timespec utc_time{1700000000, 250000000};
  timespec ptp_time{1700000037, 250000000};
};

class ConstantClock final : public RobotClock {
 public:
  explicit ConstantClock(double value) : value_(value) {}
  double Now() const override {
    return value_;
  }

 private:
  double value_;
};

TEST(RobotClockTest, UnknownOffsetFallsBackWithoutOpeningHardware) {
  auto io = std::make_shared<FakeClockIo>();
  auto clock = internal::MakeRobotClockWithIo({}, io);
  ASSERT_TRUE(clock.ok()) << clock.status();
  EXPECT_EQ((*clock)->SourceName(), "UTC");
  EXPECT_EQ((*clock)->Now(), 1700000000.25 - kRobotStartTime);
  EXPECT_EQ(io->opens, 0);
}

TEST(RobotClockTest, PtpConvertsToUtcAndOwnsDescriptor) {
  auto io = std::make_shared<FakeClockIo>();
  config::RobotClockConfig config;
  config.set_ptp_device("/test/ptp");
  config.set_ptp_utc_offset_seconds(37);  // Test fixture, not a platform default.
  {
    auto clock = internal::MakeRobotClockWithIo(config, io);
    ASSERT_TRUE(clock.ok()) << clock.status();
    EXPECT_EQ((*clock)->SourceName(), "PTP");
    EXPECT_EQ((*clock)->Now(), 1700000000.25 - kRobotStartTime);
    EXPECT_EQ(io->opened_path, "/test/ptp");
    EXPECT_EQ(io->last_id, internal::PtpClockId(7));
    EXPECT_EQ(io->utc_reads, 0);
    EXPECT_EQ(io->closes, 0);
  }
  EXPECT_EQ(io->closes, 1);
}

TEST(RobotClockTest, ExplicitZeroOffsetAndDefaultPtpPath) {
  auto io = std::make_shared<FakeClockIo>();
  config::RobotClockConfig config;
  config.set_ptp_utc_offset_seconds(0);
  auto clock = internal::MakeRobotClockWithIo(config, io);
  ASSERT_TRUE(clock.ok()) << clock.status();
  EXPECT_EQ((*clock)->Now(), 1700000037.25 - kRobotStartTime);
  EXPECT_EQ(io->opened_path, "/dev/ptp0");
}

TEST(RobotClockTest, PtpOpenFailureFallsBackUnlessRequired) {
  auto io = std::make_shared<FakeClockIo>();
  io->open_status = absl::PermissionDeniedError("test permission denied");
  config::RobotClockConfig config;
  config.set_ptp_utc_offset_seconds(37);
  auto clock = internal::MakeRobotClockWithIo(config, io);
  ASSERT_TRUE(clock.ok()) << clock.status();
  EXPECT_EQ((*clock)->SourceName(), "UTC");
  const int utc_reads = io->utc_reads;
  config.set_require_ptp(true);
  EXPECT_EQ(internal::MakeRobotClockWithIo(config, io).status().code(),
            absl::StatusCode::kPermissionDenied);
  EXPECT_EQ(io->utc_reads, utc_reads);
  EXPECT_EQ(io->closes, 0);
}

TEST(RobotClockTest, PtpProbeFailureClosesDescriptorBeforeFallback) {
  auto io = std::make_shared<FakeClockIo>();
  io->ptp_status = absl::UnavailableError("test PHC unavailable");
  config::RobotClockConfig config;
  config.set_ptp_utc_offset_seconds(37);
  auto clock = internal::MakeRobotClockWithIo(config, io);
  ASSERT_TRUE(clock.ok()) << clock.status();
  EXPECT_EQ((*clock)->SourceName(), "UTC");
  EXPECT_EQ(io->closes, 1);
  config.set_require_ptp(true);
  EXPECT_FALSE(internal::MakeRobotClockWithIo(config, io).ok());
  EXPECT_EQ(io->closes, 2);
}

TEST(RobotClockTest, RuntimeFailureDoesNotSwitchSources) {
  auto io = std::make_shared<FakeClockIo>();
  config::RobotClockConfig config;
  config.set_ptp_utc_offset_seconds(37);
  auto clock = internal::MakeRobotClockWithIo(config, io);
  ASSERT_TRUE(clock.ok()) << clock.status();
  io->ptp_status = absl::UnavailableError("test PHC disconnected");
  EXPECT_THROW((*clock)->Now(), std::runtime_error);
  EXPECT_EQ(io->utc_reads, 0);
  EXPECT_EQ((*clock)->SourceName(), "PTP");
}

TEST(RobotClockTest, UtcProbeAndRuntimeFailuresAreReported) {
  auto io = std::make_shared<FakeClockIo>();
  config::RobotClockConfig config;
  config.set_source(config::RobotClockConfig::UTC);
  auto clock = internal::MakeRobotClockWithIo(config, io);
  ASSERT_TRUE(clock.ok()) << clock.status();
  io->utc_status = absl::UnavailableError("test UTC unavailable");
  EXPECT_THROW((*clock)->Now(), std::runtime_error);
  EXPECT_FALSE(internal::MakeRobotClockWithIo(config, io).ok());
  EXPECT_EQ(io->opens, 0);
}

TEST(RobotClockTest, MonotonicRequiresExplicitSelection) {
  auto io = std::make_shared<FakeClockIo>();
  config::RobotClockConfig config;
  config.set_source(config::RobotClockConfig::MONOTONIC);
  auto clock = internal::MakeRobotClockWithIo(config, io);
  ASSERT_TRUE(clock.ok()) << clock.status();
  EXPECT_EQ((*clock)->Now(), 123.5 - kRobotStartTime);
  EXPECT_EQ(io->last_id, CLOCK_MONOTONIC);
  EXPECT_EQ(io->opens, 0);
}

TEST(RobotClockTest, RejectsInvalidConfigurationWithoutIo) {
  auto io = std::make_shared<FakeClockIo>();
  config::RobotClockConfig config;
  config.set_require_ptp(true);
  EXPECT_FALSE(internal::MakeRobotClockWithIo(config, io).ok());
  config.set_ptp_utc_offset_seconds(37);
  EXPECT_TRUE(ValidateRobotClockConfig(config).ok());
  config.set_source(config::RobotClockConfig::UTC);
  EXPECT_FALSE(internal::MakeRobotClockWithIo(config, io).ok());
  config.Clear();
  config.set_source(static_cast<config::RobotClockConfig::Source>(99));
  EXPECT_FALSE(internal::MakeRobotClockWithIo(config, io).ok());
  config.Clear();
  config.set_ptp_device(std::string("/test/ptp\0ignored", 17));
  EXPECT_FALSE(internal::MakeRobotClockWithIo(config, io).ok());
  EXPECT_EQ(io->opens, 0);
  EXPECT_EQ(io->utc_reads, 0);
}

// Every global-state scenario gets a fresh process; no production reset hook.
class GlobalRobotClockTest : public testing::Test {
 protected:
  void SetUp() override {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
  }
};

TEST_F(GlobalRobotClockTest, ExplicitInstallIsSharedByThreadsAndHelpers) {
  ASSERT_EXIT(
      {
        if (SetGlobalRobotClock(nullptr).ok()) _exit(1);
        if (!SetGlobalRobotClock(std::make_unique<ConstantClock>(42.125)).ok()) _exit(2);
        std::atomic<int> errors{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 16; ++i) {
          threads.emplace_back([&] {
            for (int j = 0; j < 1000; ++j) {
              if (RobotTime() != 42.125 || RobotTimeFromHelper() != 42.125) ++errors;
            }
          });
        }
        for (auto& thread : threads) thread.join();
        if (SetGlobalRobotClock(std::make_unique<ConstantClock>(99)).ok()) _exit(3);
        _exit(errors == 0 && RobotTime() == 42.125 ? 0 : 4);
      },
      testing::ExitedWithCode(0),
      "");
}

TEST_F(GlobalRobotClockTest, LazyInitializationIsConcurrentAndPreventsReplacement) {
  ASSERT_EXIT(
      {
        std::atomic<int> errors{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 16; ++i) {
          threads.emplace_back([&] {
            if (!std::isfinite(RobotTime()) || RobotTime() <= 0) ++errors;
          });
        }
        for (auto& thread : threads) thread.join();
        if (SetGlobalRobotClock(std::make_unique<ConstantClock>(42)).ok()) _exit(1);
        _exit(errors == 0 ? 0 : 2);
      },
      testing::ExitedWithCode(0),
      "");
}

TEST_F(GlobalRobotClockTest, ConcurrentSettersHaveExactlyOneWinner) {
  ASSERT_EXIT(
      {
        std::atomic<int> winners{0};
        std::atomic<int> winner{-1};
        std::atomic<bool> start{false};
        std::vector<std::thread> threads;
        for (int i = 0; i < 16; ++i) {
          threads.emplace_back([&, i] {
            while (!start.load()) std::this_thread::yield();
            if (SetGlobalRobotClock(std::make_unique<ConstantClock>(i)).ok()) {
              ++winners;
              winner = i;
            }
          });
        }
        start = true;
        for (auto& thread : threads) thread.join();
        _exit(winners == 1 && RobotTime() == winner ? 0 : 1);
      },
      testing::ExitedWithCode(0),
      "");
}

TEST_F(GlobalRobotClockTest, SetterAndLazyReaderRaceWithoutReplacingWinner) {
  ASSERT_EXIT(
      {
        std::atomic<bool> start{false};
        bool installed = false;
        double observed = 0;
        std::thread setter([&] {
          while (!start.load()) std::this_thread::yield();
          installed = SetGlobalRobotClock(std::make_unique<ConstantClock>(42)).ok();
        });
        std::thread reader([&] {
          while (!start.load()) std::this_thread::yield();
          observed = RobotTime();
        });
        start = true;
        setter.join();
        reader.join();
        if (installed && (observed != 42 || RobotTime() != 42)) _exit(1);
        if (!installed && (observed < 1000 || RobotTime() < 1000)) _exit(2);
        _exit(0);
      },
      testing::ExitedWithCode(0),
      "");
}

}  // namespace
}  // namespace joshua
