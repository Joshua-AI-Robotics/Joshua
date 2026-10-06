// Tests the framed serial adapter through allocated pseudo-terminals, never real
// serial devices. Covers variable-length/fragmented frames, stale-input flush,
// bounded read deadlines, invalid lengths and disconnects; protocol-level
// session/correlation behavior is tested by joshua_wire_session_test.cc.
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "firmware/common/joshua_wire.h"
#include "gtest/gtest.h"
#include "robot/comm/serial/framed_serial_transport.h"
#include "robot/comm/serial/serial.h"

namespace robot::comm {
namespace {
using Bytes = std::vector<uint8_t>;

class SerialExchangeTest : public ::testing::Test {
 protected:
  int master = -1;
  std::shared_ptr<boost::asio::io_context> io = std::make_shared<boost::asio::io_context>();
  std::shared_ptr<Serial> link;
  std::unique_ptr<FramedSerialTransport> serial;
  Bytes request;
  void SetUp() override {
    // An allocated PTY only: no real /dev/ttyACM*, ttyUSB* or NIC is opened.
    master = posix_openpt(O_RDWR | O_NOCTTY);
    ASSERT_GE(master, 0);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    link = std::make_shared<Serial>(io, ptsname(master), 115200);
    serial = std::make_unique<FramedSerialTransport>(link, std::chrono::milliseconds(100));
    request.resize(JW_MAX_FRAME_LEN);
    const int len = jw_encode_frame(request.data(), request.size(), 100, 5, 1, 0xff, nullptr, 0);
    request.resize(len);
  }
  void TearDown() override {
    serial.reset();
    link.reset();
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
    Bytes bytes(JW_MAX_FRAME_LEN);
    Bytes payload(payload_size, 42);
    const int len = jw_encode_frame(
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

TEST_F(SerialExchangeTest, ConfiguredDeadlineReplacesTheDefault) {
  serial = std::make_unique<FramedSerialTransport>(link, std::chrono::milliseconds(25));
  const auto start = std::chrono::steady_clock::now();
  EXPECT_EQ(serial->Exchange(request).status().code(), absl::StatusCode::kDeadlineExceeded);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_GE(elapsed, std::chrono::milliseconds(25));
  EXPECT_LT(elapsed, std::chrono::seconds(1));
  ASSERT_TRUE(ReadRequest());
  serial = std::make_unique<FramedSerialTransport>(link, std::chrono::milliseconds(500));
  auto response = Reply(1);
  auto device = std::async(std::launch::async, [&] {
    if (!ReadRequest()) return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    return write(master, response.data(), response.size()) == static_cast<ssize_t>(response.size());
  });
  auto actual = serial->Exchange(request);
  EXPECT_TRUE(device.get());
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(*actual, response);
}

TEST_F(SerialExchangeTest, BusLockWaitIsBoundedAndExpiredRequestIsNotSent) {
  FramedSerialTransport slow(link, std::chrono::milliseconds(300));
  FramedSerialTransport fast(link, std::chrono::milliseconds(25));
  auto first = std::async(std::launch::async, [&] { return slow.Exchange(request); });
  ASSERT_TRUE(ReadRequest());  // The first transaction now owns the physical bus.
  auto second = fast.Exchange(request);
  EXPECT_EQ(second.status().code(), absl::StatusCode::kDeadlineExceeded);
  EXPECT_NE(second.status().message().find("not sent"), std::string::npos);
  pollfd descriptor{master, POLLIN, 0};
  EXPECT_EQ(poll(&descriptor, 1, 10), 0);
  EXPECT_EQ(first.get().status().code(), absl::StatusCode::kDeadlineExceeded);
}

TEST_F(SerialExchangeTest, RawMechanismDoesNotKnowJoshuaWireFraming) {
  request = {0x42};
  auto device = std::async(std::launch::async, [&] {
    if (!ReadRequest()) return false;
    const uint8_t response[] = {0x10, 0x20};
    return write(master, response, sizeof(response)) == sizeof(response);
  });
  Bytes response;
  auto status = link->ExchangeUntil(
      request, std::chrono::milliseconds(100), [&](uint8_t byte) -> absl::StatusOr<bool> {
        response.push_back(byte);
        return response.size() == 2;
      });
  EXPECT_TRUE(device.get());
  EXPECT_TRUE(status.ok()) << status;
  EXPECT_EQ(response, (Bytes{0x10, 0x20}));
}

TEST_F(SerialExchangeTest, InvalidDeadlineDoesNotWrite) {
  for (auto timeout : {std::chrono::milliseconds(0), std::chrono::milliseconds(-1)}) {
    FramedSerialTransport invalid(link, timeout);
    EXPECT_EQ(invalid.Exchange(request).status().code(), absl::StatusCode::kInvalidArgument);
  }
  pollfd descriptor{master, POLLIN, 0};
  EXPECT_EQ(poll(&descriptor, 1, 10), 0);
}

TEST_F(SerialExchangeTest, LegacyAndSendOnlyBytesAreForwardedUnchanged) {
  // Vendor callers still supply their exact response length, not JW framing.
  request = {0xff, 0xff, 0x01, 0x02};
  const Bytes response = {0xff, 0xff, 0x01, 0x00, 0x03};
  auto device = std::async(std::launch::async, [&] {
    if (!ReadRequest()) return false;
    return write(master, response.data(), response.size()) == static_cast<ssize_t>(response.size());
  });
  auto actual = serial->SendAndReceive(request, response.size());
  EXPECT_TRUE(device.get());
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(*actual, response);
  EXPECT_TRUE(serial->Send(request).ok());
  EXPECT_TRUE(ReadRequest());
}
}  // namespace
}  // namespace robot::comm
