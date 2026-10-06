#include "robot/comm/serial/serial.h"

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <boost/asio.hpp>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "absl/status/status.h"
#include "absl/time/time.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"
#include "robot/comm/interfaces/message_framer.h"

namespace robot::comm {
namespace {

// The first byte counts the bytes that follow it.
class LengthPrefixFramer : public MessageFramer {
 public:
  absl::StatusOr<size_t> RemainingBytes(absl::Span<const uint8_t> received) const override {
    if (received.empty()) return 1;
    return static_cast<size_t>(received[0]) + 1 - received.size();
  }
};

class RejectingFramer : public MessageFramer {
 public:
  absl::StatusOr<size_t> RemainingBytes(absl::Span<const uint8_t> received) const override {
    if (received.empty()) return 1;
    return absl::DataLossError("bad header");
  }
};

// A pseudo-terminal: Serial opens the follower side, the test plays the device
// on the controller side.
class SerialPtyTest : public ::testing::Test {
 protected:
  void SetUp() override {
    controller_fd_ = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_GE(controller_fd_, 0);
    ASSERT_EQ(grantpt(controller_fd_), 0);
    ASSERT_EQ(unlockpt(controller_fd_), 0);
    const char* follower = ptsname(controller_fd_);
    ASSERT_NE(follower, nullptr);
    io_thread_ = std::thread([this] { io_->run(); });
    serial_ = std::make_unique<Serial>(io_, follower, 115200);
  }

  void TearDown() override {
    serial_.reset();
    work_guard_.reset();
    if (io_thread_.joinable()) io_thread_.join();
    if (controller_fd_ >= 0) close(controller_fd_);
  }

  // Reads exactly `size` bytes the host wrote, or fewer on a 1 s timeout.
  std::vector<uint8_t> DeviceRead(size_t size) {
    std::vector<uint8_t> bytes;
    while (bytes.size() < size) {
      pollfd fd{controller_fd_, POLLIN, 0};
      if (poll(&fd, 1, 1000) <= 0) break;
      uint8_t buf[64];
      const ssize_t n = read(controller_fd_, buf, std::min(sizeof(buf), size - bytes.size()));
      if (n <= 0) break;
      bytes.insert(bytes.end(), buf, buf + n);
    }
    return bytes;
  }

  void DeviceWrite(const std::vector<uint8_t>& bytes) {
    ASSERT_EQ(write(controller_fd_, bytes.data(), bytes.size()),
              static_cast<ssize_t>(bytes.size()));
  }

  int controller_fd_ = -1;
  std::shared_ptr<boost::asio::io_context> io_ = std::make_shared<boost::asio::io_context>();
  boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_ =
      boost::asio::make_work_guard(*io_);
  std::thread io_thread_;
  std::unique_ptr<Serial> serial_;
};

TEST_F(SerialPtyTest, ExchangeReadsExactlyOneFramedResponse) {
  const std::vector<uint8_t> request = {0xA5, 0x01};
  std::vector<uint8_t> seen_request;
  std::thread device([&] {
    seen_request = DeviceRead(request.size());
    DeviceWrite({0x03, 0x10, 0x20, 0x30, 0xEE});
  });

  auto response = serial_->Exchange(request, LengthPrefixFramer(), absl::Milliseconds(500));
  device.join();

  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, (std::vector<uint8_t>{0x03, 0x10, 0x20, 0x30}));
  EXPECT_EQ(seen_request, request);
}

TEST_F(SerialPtyTest, ExchangeDiscardsInputReceivedBeforeTheRequest) {
  DeviceWrite({0x02, 0xBA, 0xD0});
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  const std::vector<uint8_t> request = {0x07};
  std::thread device([&] {
    DeviceRead(request.size());
    DeviceWrite({0x01, 0x42});
  });

  auto response = serial_->Exchange(request, LengthPrefixFramer(), absl::Milliseconds(500));
  device.join();

  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, (std::vector<uint8_t>{0x01, 0x42}));
}

TEST_F(SerialPtyTest, ExchangeTimesOutWhenTheDeviceIsSilent) {
  const std::vector<uint8_t> request = {0x07};
  const auto start = std::chrono::steady_clock::now();

  auto response = serial_->Exchange(request, LengthPrefixFramer(), absl::Milliseconds(30));

  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_EQ(response.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_LT(elapsed, std::chrono::milliseconds(500));
}

TEST_F(SerialPtyTest, ExchangeTimesOutOnATruncatedResponse) {
  const std::vector<uint8_t> request = {0x07};
  std::thread device([&] {
    DeviceRead(request.size());
    DeviceWrite({0x04, 0x01});
  });

  auto response = serial_->Exchange(request, LengthPrefixFramer(), absl::Milliseconds(50));
  device.join();

  EXPECT_EQ(response.status().code(), absl::StatusCode::kDeadlineExceeded);
}

TEST_F(SerialPtyTest, ExchangeRecoversAfterATimeout) {
  const std::vector<uint8_t> request = {0x07};
  ASSERT_EQ(
      serial_->Exchange(request, LengthPrefixFramer(), absl::Milliseconds(20)).status().code(),
      absl::StatusCode::kDeadlineExceeded);
  DeviceRead(request.size());
  std::thread device([&] {
    DeviceRead(request.size());
    DeviceWrite({0x01, 0x55});
  });

  auto response = serial_->Exchange(request, LengthPrefixFramer(), absl::Milliseconds(500));
  device.join();

  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(*response, (std::vector<uint8_t>{0x01, 0x55}));
}

TEST_F(SerialPtyTest, ExchangePropagatesFramerRejection) {
  const std::vector<uint8_t> request = {0x07};
  std::thread device([&] {
    DeviceRead(request.size());
    DeviceWrite({0xFF});
  });

  auto response = serial_->Exchange(request, RejectingFramer(), absl::Milliseconds(500));
  device.join();

  EXPECT_EQ(response.status().code(), absl::StatusCode::kDataLoss);
}

TEST_F(SerialPtyTest, SendWritesTheRequest) {
  const std::vector<uint8_t> request = {0xFF, 0xFF, 0x05};

  ASSERT_TRUE(serial_->Send(request, absl::Milliseconds(500)).ok());

  EXPECT_EQ(DeviceRead(request.size()), request);
}

TEST_F(SerialPtyTest, RejectsNonPositiveTimeout) {
  const std::vector<uint8_t> request = {0x07};

  EXPECT_EQ(serial_->Send(request, absl::ZeroDuration()).code(),
            absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(
      serial_->Exchange(request, LengthPrefixFramer(), absl::InfiniteDuration()).status().code(),
      absl::StatusCode::kInvalidArgument);
}

}  // namespace
}  // namespace robot::comm
