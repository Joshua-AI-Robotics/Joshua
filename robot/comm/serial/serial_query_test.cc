// PTYs only: never opens a physical serial device.
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <thread>

#include "gtest/gtest.h"
#include "robot/comm/serial/serial.h"

namespace robot::comm {
namespace {
class SerialQueryTest : public testing::Test {
 protected:
  int master = -1;
  std::unique_ptr<Serial> serial;
  void SetUp() override {
    master = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_GE(master, 0);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    serial = std::make_unique<Serial>(
        std::make_shared<boost::asio::io_context>(), ptsname(master), 115200);
  }
  void TearDown() override {
    serial.reset();
    if (master >= 0) close(master);
  }
  bool AwaitCommand() {
    pollfd fd{master, POLLIN, 0};
    if (poll(&fd, 1, 1000) <= 0) return false;
    uint8_t command;
    return read(master, &command, 1) == 1;
  }
};
TEST_F(SerialQueryTest, SilentPeerTimesOutWithoutAnIoContextThread) {
  const auto start = std::chrono::steady_clock::now();
  const auto result = serial->AtomicRead({1}, 4);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(1));
}
TEST_F(SerialQueryTest, ReturnsCompleteResponse) {
  std::thread peer([&] {
    ASSERT_TRUE(AwaitCommand());
    const uint8_t response[] = {2, 3, 4};
    ASSERT_EQ(write(master, response, sizeof(response)), 3);
  });
  const auto result = serial->AtomicRead({1}, 3);
  peer.join();
  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, (std::vector<uint8_t>{2, 3, 4}));
}
TEST_F(SerialQueryTest, PartialResponseTimesOutAndNextQueryRecovers) {
  std::thread peer([&] {
    ASSERT_TRUE(AwaitCommand());
    const uint8_t response = 2;
    ASSERT_EQ(write(master, &response, 1), 1);
  });
  const auto result = serial->AtomicRead({1}, 4);
  peer.join();
  EXPECT_EQ(result.status().code(), absl::StatusCode::kDeadlineExceeded);
  // No stale timer callback may cancel a subsequent query.
  std::thread next([&] {
    ASSERT_TRUE(AwaitCommand());
    const uint8_t response = 9;
    ASSERT_EQ(write(master, &response, 1), 1);
  });
  const auto recovered = serial->AtomicRead({1}, 1);
  next.join();
  ASSERT_TRUE(recovered.ok()) << recovered.status();
  EXPECT_EQ(*recovered, (std::vector<uint8_t>{9}));
}
}  // namespace
}  // namespace robot::comm
