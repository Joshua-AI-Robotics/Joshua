// Tests Linux Serial::Exchange through allocated pseudo-terminals, never real
// serial devices. Covers variable-length/fragmented frames, stale-input flush,
// bounded read deadlines, invalid lengths and disconnects; protocol-level
// session/correlation behavior is tested by joshua_wire_v2_session_test.cc.
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <future>
#include <memory>
#include <vector>

#include "firmware/common/joshua_wire_v2.h"
#include "gtest/gtest.h"
#include "robot/comm/serial/serial.h"

namespace robot::comm {
namespace {
using Bytes = std::vector<uint8_t>;

class SerialExchangeTest : public ::testing::Test {
 protected:
  int master = -1;
  std::shared_ptr<boost::asio::io_context> io = std::make_shared<boost::asio::io_context>();
  std::unique_ptr<Serial> serial;
  Bytes request;
  void SetUp() override {
    // An allocated PTY only: no real /dev/ttyACM*, ttyUSB* or NIC is opened.
    master = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_GE(master, 0);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    serial = std::make_unique<Serial>(io, ptsname(master), 115200);
    request.resize(JW2_MAX_FRAME_LEN);
    const int len = jw2_encode_frame(request.data(), request.size(), 100, 5, 1, 0xff, nullptr, 0);
    request.resize(len);
  }
  void TearDown() override {
    serial.reset();
    if (master >= 0) close(master);
  }
  bool ReadRequest() {
    Bytes observed;
    while (observed.size() < request.size()) {
      pollfd descriptor{master, POLLIN, 0};
      if (poll(&descriptor, 1, 1000) <= 0) return false;
      uint8_t byte;
      if (read(master, &byte, 1) != 1) return false;
      observed.push_back(byte);
    }
    return observed == request;
  }
  Bytes Reply(size_t payload_size) {
    Bytes bytes(JW2_MAX_FRAME_LEN);
    Bytes payload(payload_size, 42);
    const int len = jw2_encode_frame(
        bytes.data(), bytes.size(), 100, 5, 1, 0xff, payload.data(), payload.size());
    bytes.resize(len);
    return bytes;
  }
};

TEST_F(SerialExchangeTest, ReadsVariableResponseLengthsAndFragmentedInput) {
  for (size_t size : {1, 26, 49}) {
    const auto response = Reply(size);
    auto device = std::async(std::launch::async, [&] {
      if (!ReadRequest()) return false;
      const uint8_t boot_noise[] = {0x12, 0x34};
      if (write(master, boot_noise, sizeof(boot_noise)) != sizeof(boot_noise)) return false;
      for (auto byte : response) {
        if (write(master, &byte, 1) != 1) return false;
      }
      return true;
    });
    auto actual = serial->Exchange(request);
    EXPECT_TRUE(device.get());
    ASSERT_TRUE(actual.ok()) << actual.status();
    EXPECT_EQ(*actual, response);
  }
}

TEST_F(SerialExchangeTest, FlushesPreexistingResponseBeforeHandshake) {
  auto stale = Reply(1);
  ASSERT_EQ(write(master, stale.data(), stale.size()), stale.size());
  // Drain visibility through the slave's input queue before the exchange.
  auto response = Reply(26);
  auto device = std::async(std::launch::async, [&] {
    if (!ReadRequest()) return false;
    return write(master, response.data(), response.size()) == static_cast<ssize_t>(response.size());
  });
  auto actual = serial->Exchange(request);
  EXPECT_TRUE(device.get());
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(*actual, response);
}

TEST_F(SerialExchangeTest, MissingAndPartialResponsesHaveBoundedDeadline) {
  const auto start = std::chrono::steady_clock::now();
  auto result = serial->Exchange(request);
  EXPECT_EQ(result.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
  ASSERT_TRUE(ReadRequest());
  auto device = std::async(std::launch::async, [&] {
    if (!ReadRequest()) return false;
    const uint8_t partial[] = {0xa5, 0x0c, 0x02};
    return write(master, partial, sizeof(partial)) == sizeof(partial);
  });
  result = serial->Exchange(request);
  EXPECT_TRUE(device.get());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kDeadlineExceeded);
}

TEST_F(SerialExchangeTest, InvalidLengthsAndDisconnectFail) {
  auto device = std::async(std::launch::async, [&] {
    if (!ReadRequest()) return false;
    const uint8_t oversized[] = {0xa5, 0xff};
    return write(master, oversized, sizeof(oversized)) == sizeof(oversized);
  });
  auto result = serial->Exchange(request);
  EXPECT_TRUE(device.get());
  EXPECT_EQ(result.status().code(), absl::StatusCode::kDataLoss);
  close(master);
  master = -1;
  EXPECT_EQ(serial->Exchange(request).status().code(), absl::StatusCode::kUnavailable);
  EXPECT_EQ(serial->Exchange({}).status().code(), absl::StatusCode::kInvalidArgument);
}
}  // namespace
}  // namespace robot::comm
