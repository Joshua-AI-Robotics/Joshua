#include "robot/perception/lidar/lds01_driver.h"

#include <cstdint>
#include <memory>
#include <vector>

#include "absl/status/status.h"
#include "gtest/gtest.h"

namespace robot::perception {
namespace {

class RecordingStream : public robot::comm::ByteStream {
 public:
  absl::Status Open() override {
    return open_status;
  }

  absl::Status Write(const std::vector<uint8_t>& data) override {
    if (data == std::vector<uint8_t>{'b'}) {
      ++start_attempts;
      return start_status;
    }
    if (data == std::vector<uint8_t>{'e'}) {
      ++stop_attempts;
      if (stop_failures > 0) {
        --stop_failures;
        return absl::UnavailableError("stop write failed");
      }
      return absl::OkStatus();
    }
    return absl::InvalidArgumentError("unexpected command");
  }

  absl::StatusOr<std::vector<uint8_t>> Read(size_t) override {
    ++read_attempts;
    return absl::UnavailableError("unexpected read");
  }

  absl::Status open_status = absl::OkStatus();
  absl::Status start_status = absl::OkStatus();
  int start_attempts = 0;
  int stop_attempts = 0;
  int stop_failures = 0;
  int read_attempts = 0;
};

TEST(Lds01DriverTest, FailedStopHaltsCollectionAndRetriesUntilSuccessful) {
  auto stream = std::make_shared<RecordingStream>();
  Lds01Driver driver(stream, SinglePerception{});
  ASSERT_TRUE(driver.Init().ok());
  stream->stop_failures = 2;

  const auto status = driver.Teardown();
  EXPECT_EQ(status.code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(status.message(), "stop write failed");
  EXPECT_TRUE(driver.GetData().ok());
  EXPECT_EQ(stream->read_attempts, 0);
  EXPECT_EQ(driver.Teardown().code(), absl::StatusCode::kUnavailable);
  EXPECT_TRUE(driver.Teardown().ok());
  EXPECT_TRUE(driver.Teardown().ok());
  EXPECT_EQ(stream->stop_attempts, 3);
}

TEST(Lds01DriverTest, DestructorRetriesFailedExplicitStop) {
  auto stream = std::make_shared<RecordingStream>();
  {
    Lds01Driver driver(stream, SinglePerception{});
    ASSERT_TRUE(driver.Init().ok());
    stream->stop_failures = 1;
    EXPECT_FALSE(driver.Teardown().ok());
    EXPECT_EQ(stream->stop_attempts, 1);
  }
  EXPECT_EQ(stream->stop_attempts, 2);
}

TEST(Lds01DriverTest, SuccessfulStopIsNotRepeatedByDestructor) {
  auto stream = std::make_shared<RecordingStream>();
  {
    Lds01Driver driver(stream, SinglePerception{});
    ASSERT_TRUE(driver.Init().ok());
    EXPECT_TRUE(driver.Teardown().ok());
  }
  EXPECT_EQ(stream->stop_attempts, 1);
}

TEST(Lds01DriverTest, FailedStartStillAttemptsMotorCleanup) {
  auto stream = std::make_shared<RecordingStream>();
  stream->start_status = absl::UnavailableError("start write failed");
  {
    Lds01Driver driver(stream, SinglePerception{});
    EXPECT_EQ(driver.Init().code(), absl::StatusCode::kUnavailable);
  }
  EXPECT_EQ(stream->start_attempts, 1);
  EXPECT_EQ(stream->stop_attempts, 1);
}

TEST(Lds01DriverTest, FailedOpenDoesNotSendMotorCommands) {
  auto stream = std::make_shared<RecordingStream>();
  stream->open_status = absl::UnavailableError("open failed");
  {
    Lds01Driver driver(stream, SinglePerception{});
    EXPECT_EQ(driver.Init().code(), absl::StatusCode::kUnavailable);
    EXPECT_TRUE(driver.Teardown().ok());
  }
  EXPECT_EQ(stream->start_attempts, 0);
  EXPECT_EQ(stream->stop_attempts, 0);
}

TEST(Lds01DriverTest, ReinitializationRequiresAnotherMotorStop) {
  auto stream = std::make_shared<RecordingStream>();
  {
    Lds01Driver driver(stream, SinglePerception{});
    ASSERT_TRUE(driver.Init().ok());
    ASSERT_TRUE(driver.Teardown().ok());
    ASSERT_TRUE(driver.Init().ok());
  }
  EXPECT_EQ(stream->start_attempts, 2);
  EXPECT_EQ(stream->stop_attempts, 2);
}

}  // namespace
}  // namespace robot::perception
