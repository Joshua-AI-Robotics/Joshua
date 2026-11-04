// Linux PTYs only: no physical serial device is opened by these tests.
#include "robot/comm/serial/serial_io_owner.h"

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "robot/comm/serial/serial.h"

namespace robot::comm {
namespace {
using namespace std::chrono_literals;
using Owner = SerialIoOwner;
using Bytes = std::vector<uint8_t>;
using Clock = std::chrono::steady_clock;

class SerialIoOwnerTest : public ::testing::Test {
 protected:
  int master = -1;
  std::string path;
  std::shared_ptr<Owner> owner;
  void SetUp() override {
    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    ASSERT_GE(master, 0);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    path = ptsname(master);
    Open();
  }
  void Open(SerialIoOptions options = {}) {
    auto opened = Owner::Open(path, 230400, 0ms, options);
    ASSERT_TRUE(opened.ok()) << opened.status();
    owner = *opened;
  }
  void TearDown() override {
    owner.reset();
    if (master >= 0) close(master);
  }
  static Owner::Request Request(Bytes bytes = {},
                                size_t response = 1,
                                std::chrono::milliseconds timeout = 2s) {
    Owner::Request r;
    r.bytes = std::move(bytes);
    r.response_bytes = response;
    r.timeout = timeout;
    return r;
  }
  bool Observe(const Bytes& expected) {
    Bytes actual(expected.size());
    size_t offset = 0;
    while (offset < actual.size()) {
      pollfd fd{master, POLLIN, 0};
      if (poll(&fd, 1, 1000) <= 0) return false;
      const auto n = read(master, actual.data() + offset, actual.size() - offset);
      if (n <= 0) return false;
      offset += n;
    }
    return actual == expected;
  }
  bool Send(const Bytes& bytes) {
    return write(master, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size());
  }
  void ExpectUnsent() {
    pollfd fd{master, POLLIN, 0};
    EXPECT_EQ(poll(&fd, 1, 10), 0);
  }
};

TEST_F(SerialIoOwnerTest, QueuedDeadlineExpiresDuringAnotherOperationWithoutPublication) {
  auto first = owner->Submit(Request({1}));
  ASSERT_TRUE(Observe({1}));
  const auto start = Clock::now();
  auto queued = owner->Submit(Request({2}, 1, 40ms));
  auto result = queued.Wait();
  EXPECT_TRUE(absl::IsDeadlineExceeded(result.status));
  EXPECT_EQ(result.publication, Owner::Publication::kNotSent);
  EXPECT_LT(Clock::now() - start, 1s);
  EXPECT_FALSE(first.Ready());
  ExpectUnsent();
  first.Cancel();
  EXPECT_TRUE(absl::IsCancelled(first.Wait().status));
}

TEST_F(SerialIoOwnerTest, CancellationDistinguishesUnsentAndPublishedRequests) {
  auto first = owner->Submit(Request({1}));
  ASSERT_TRUE(Observe({1}));
  auto queued = owner->Submit(Request({2}));
  queued.Cancel();
  auto unsent = queued.Wait();
  EXPECT_TRUE(absl::IsCancelled(unsent.status));
  EXPECT_EQ(unsent.bytes_written, 0);
  EXPECT_EQ(unsent.publication, Owner::Publication::kNotSent);
  first.Cancel();
  auto published = first.Wait();
  EXPECT_TRUE(absl::IsCancelled(published.status));
  EXPECT_EQ(published.bytes_written, 1);
  EXPECT_EQ(published.publication, Owner::Publication::kMayHaveReachedDevice);
  queued.Cancel();  // Completed operations are stable and repeatable.
  EXPECT_EQ(queued.Wait().status, unsent.status);
  ExpectUnsent();
}

TEST_F(SerialIoOwnerTest, CloseWakesActiveAndQueuedWaitersAndOldGenerationStaysClosed) {
  const auto generation = owner->Snapshot().generation;
  auto first = owner->Submit(Request({1}, 1, 30s));
  ASSERT_TRUE(Observe({1}));
  auto queued = owner->Submit(Request({2}, 1, 30s));
  const auto start = Clock::now();
  ASSERT_TRUE(owner->Close().ok());
  EXPECT_LT(Clock::now() - start, 1s);
  EXPECT_TRUE(absl::IsCancelled(first.Wait().status));
  EXPECT_TRUE(absl::IsCancelled(queued.Wait().status));
  EXPECT_TRUE(owner->Close().ok());
  for (int i = 0; i < 4; ++i) {
    auto repeated = std::async(std::launch::async, [this] { return owner->Close(); });
    EXPECT_TRUE(repeated.get().ok());
  }
  EXPECT_EQ(owner->Snapshot().state, SerialLinkState::kClosed);
  EXPECT_EQ(owner->Snapshot().pending_operations, 0);
  EXPECT_TRUE(absl::IsFailedPrecondition(owner->Submit(Request({3})).Wait().status));
  EXPECT_TRUE(absl::IsAlreadyExists(Owner::Open(path, 230400).status()));
  owner.reset();
  Open();
  EXPECT_GT(owner->Snapshot().generation, generation);
  EXPECT_TRUE(owner->Submit(Request({4}, 0)).Wait().status.ok());
  EXPECT_TRUE(Observe({4}));
}

TEST_F(SerialIoOwnerTest, OperationAndByteAdmissionLimitsRejectBeforePublication) {
  owner.reset();
  SerialIoOptions options;
  options.max_pending_operations = 2;
  options.max_pending_bytes = 10;
  options.max_transfer_bytes = 10;
  Open(options);
  auto active = owner->Submit(Request({1}, 5));
  ASSERT_TRUE(Observe({1}));
  auto too_large = owner->Submit(Request(Bytes(11), 0)).Wait();
  EXPECT_TRUE(absl::IsResourceExhausted(too_large.status));
  auto too_many_bytes = owner->Submit(Request({2}, 4)).Wait();
  EXPECT_TRUE(absl::IsResourceExhausted(too_many_bytes.status));
  auto queued = owner->Submit(Request({3}, 1));
  EXPECT_EQ(owner->Snapshot().pending_bytes, 8);
  auto too_many_ops = owner->Submit(Request({}, 0)).Wait();
  EXPECT_TRUE(absl::IsResourceExhausted(too_many_ops.status));
  EXPECT_EQ(too_many_ops.publication, Owner::Publication::kNotSent);
  queued.Cancel();
  queued.Wait();
  active.Cancel();
  active.Wait();
  const auto d = owner->Snapshot();
  EXPECT_EQ(d.admitted, 2);
  EXPECT_EQ(d.completed, 2);
  EXPECT_EQ(d.rejected, 3);
  EXPECT_EQ(d.cancelled, 2);
  EXPECT_EQ(d.bytes_written, 1);
  EXPECT_EQ(d.pending_bytes, 0);
  EXPECT_EQ(d.pending_operations, 0);
  EXPECT_LE(d.last_error.size(), 512);
  ExpectUnsent();
}

TEST_F(SerialIoOwnerTest, BackpressuredWriteHasBoundedTotalDeadlineAndPublication) {
  const auto start = Clock::now();
  auto result = owner->Submit(Request(Bytes(65536, 0x42), 0, 60ms)).Wait();
  EXPECT_TRUE(absl::IsDeadlineExceeded(result.status));
  EXPECT_GT(result.bytes_written, 0);
  EXPECT_LT(result.bytes_written, 65536);
  EXPECT_EQ(result.publication, Owner::Publication::kMayHaveReachedDevice);
  EXPECT_LT(Clock::now() - start, 1s);
}

TEST_F(SerialIoOwnerTest, PartialReadPreservesProgressAndIndependentIdleDeadline) {
  auto request = Request({}, 3, 2s);
  request.read_idle_timeout = 40ms;
  auto operation = owner->Submit(std::move(request));
  ASSERT_TRUE(Send({0x10, 0x20}));
  const auto start = Clock::now();
  auto result = operation.Wait();
  EXPECT_TRUE(absl::IsDeadlineExceeded(result.status));
  EXPECT_EQ(result.response, (Bytes{0x10, 0x20}));
  EXPECT_EQ(result.bytes_read, 2);
  EXPECT_EQ(result.publication, Owner::Publication::kNotSent);
  EXPECT_LT(Clock::now() - start, 1s);
  EXPECT_EQ(owner->Snapshot().timed_out, 1);
  EXPECT_EQ(owner->Snapshot().bytes_read, 2);
}

TEST_F(SerialIoOwnerTest, ReadProgressDoesNotRenewTotalDeadline) {
  auto r = Request({}, 10, 110ms);
  r.read_idle_timeout = 100ms;
  auto operation = owner->Submit(std::move(r));
  auto device = std::async(std::launch::async, [&] {
    for (int i = 0; i < 6; ++i) {
      Send({42});
      std::this_thread::sleep_for(30ms);
    }
  });
  const auto start = Clock::now();
  auto result = operation.Wait();
  EXPECT_TRUE(absl::IsDeadlineExceeded(result.status));
  EXPECT_GT(result.bytes_read, 1);
  EXPECT_LT(result.bytes_read, 6);
  EXPECT_LT(Clock::now() - start, 1s);
  device.get();
}

TEST_F(SerialIoOwnerTest, DisconnectFaultsOwnerAndCompletesAllWaiters) {
  auto active = owner->Submit(Request({1}, 1, 30s));
  ASSERT_TRUE(Observe({1}));
  auto queued = owner->Submit(Request({2}, 1, 30s));
  close(master);
  master = -1;
  EXPECT_TRUE(absl::IsUnavailable(active.Wait().status));
  auto result = queued.Wait();
  EXPECT_TRUE(absl::IsUnavailable(result.status));
  EXPECT_EQ(result.publication, Owner::Publication::kNotSent);
  EXPECT_TRUE(absl::IsUnavailable(owner->status()));
  EXPECT_TRUE(absl::IsUnavailable(owner->Submit(Request()).Wait().status));
  EXPECT_EQ(owner->Snapshot().state, SerialLinkState::kFaulted);
  EXPECT_EQ(owner->Snapshot().pending_operations, 0);
}

TEST_F(SerialIoOwnerTest, FramingIsBoundedAndCannotReenterOrCloseOwner) {
  auto r = Request({}, 2);
  r.consume = [&](uint8_t) -> absl::StatusOr<bool> {
    EXPECT_TRUE(absl::IsFailedPrecondition(owner->Submit(Request()).Wait().status));
    EXPECT_TRUE(absl::IsFailedPrecondition(owner->Close()));
    return false;
  };
  auto operation = owner->Submit(std::move(r));
  ASSERT_TRUE(Send({1, 2}));
  EXPECT_TRUE(absl::IsResourceExhausted(operation.Wait().status));
  auto throwing = Request({}, 1);
  throwing.consume = [](uint8_t) -> absl::StatusOr<bool> { throw std::runtime_error("test"); };
  auto second = owner->Submit(std::move(throwing));
  ASSERT_TRUE(Send({3}));
  EXPECT_TRUE(absl::IsInternal(second.Wait().status));
  EXPECT_TRUE(owner->status().ok());
  auto third = owner->Submit(Request({}, 1));
  ASSERT_TRUE(Send({4}));
  EXPECT_EQ(third.Wait().response, (Bytes{4}));
}

TEST_F(SerialIoOwnerTest, FullLidarScanUsesConfiguredBudgetAndPreservesPendingInput) {
  owner.reset();
  auto opened = Serial::Create(path, 230400);
  ASSERT_TRUE(opened.ok()) << opened.status();
  auto serial = *opened;
  // 2520 bytes at 230400 baud exceeds the old fixed 10 ms raw read budget.
  const Bytes scan(2520, 0x55);
  ASSERT_TRUE(Send(Bytes(scan.begin(), scan.begin() + 420)));
  auto device = std::async(std::launch::async, [&] {
    for (size_t offset = 420; offset < scan.size(); offset += 420) {
      std::this_thread::sleep_for(20ms);
      if (!Send(Bytes(scan.begin() + offset, scan.begin() + offset + 420))) return false;
    }
    return true;
  });
  auto actual = serial->Read(scan.size());
  EXPECT_TRUE(device.get());
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(*actual, scan);
  EXPECT_EQ(serial->Snapshot().bytes_read, scan.size());
}

TEST_F(SerialIoOwnerTest, SeparatePortsMakeProgressIndependently) {
  auto blocked = owner->Submit(Request({1}, 1, 30s));
  ASSERT_TRUE(Observe({1}));
  int second_master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
  ASSERT_GE(second_master, 0);
  ASSERT_EQ(grantpt(second_master), 0);
  ASSERT_EQ(unlockpt(second_master), 0);
  auto opened = Owner::Open(ptsname(second_master), 115200);
  ASSERT_TRUE(opened.ok()) << opened.status();
  auto result = (*opened)->Submit(Request({2}, 0, 100ms)).Wait();
  EXPECT_TRUE(result.status.ok()) << result.status;
  EXPECT_FALSE(blocked.Ready());
  (*opened)->Close().IgnoreError();
  close(second_master);
  blocked.Cancel();
  EXPECT_TRUE(absl::IsCancelled(blocked.Wait().status));
}

TEST_F(SerialIoOwnerTest, InvalidRequestsFailBeforePublication) {
  auto invalid = Request({1});
  invalid.timeout = 0ms;
  EXPECT_TRUE(absl::IsInvalidArgument(owner->Submit(invalid).Wait().status));
  invalid.timeout = -1ms;
  EXPECT_TRUE(absl::IsInvalidArgument(owner->Submit(invalid).Wait().status));
  invalid.timeout = 1s;
  invalid.read_idle_timeout = -1ms;
  EXPECT_TRUE(absl::IsInvalidArgument(owner->Submit(invalid).Wait().status));
  invalid.read_idle_timeout = 0ms;
  invalid.input = static_cast<Owner::InputFlushPolicy>(255);
  EXPECT_TRUE(absl::IsInvalidArgument(owner->Submit(invalid).Wait().status));
  EXPECT_EQ(owner->Snapshot().admitted, 0);
  EXPECT_EQ(owner->Snapshot().rejected, 4);
  ExpectUnsent();
}

TEST_F(SerialIoOwnerTest, VendorResponseCanUseAnExplicitLongerTransactionBudget) {
  owner.reset();
  SerialIoOptions options;
  options.transaction_timeout_ms = 600;
  options.read_idle_timeout_ms = 400;
  auto opened = Serial::Create(path, 115200, 0ms, options);
  ASSERT_TRUE(opened.ok()) << opened.status();
  auto serial = *opened;
  const Bytes command{0xff, 0xff, 0x01, 0x02};
  const Bytes response(128, 0x55);
  auto device = std::async(std::launch::async, [&] {
    if (!Observe(command)) return false;
    std::this_thread::sleep_for(150ms);
    if (!Send(Bytes(response.begin(), response.begin() + 64))) return false;
    std::this_thread::sleep_for(30ms);
    return Send(Bytes(response.begin() + 64, response.end()));
  });
  auto actual = serial->AtomicRead(command, response.size());
  EXPECT_TRUE(device.get());
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(*actual, response);
  EXPECT_EQ(serial->Snapshot().bytes_written, command.size());
}

TEST(SerialIoOwnerOpenTest, FailedOpenReleasesItsProcessClaim) {
  // /dev/null is a virtual non-tty; terminal configuration must fail both times.
  auto first = Owner::Open("/dev/null", 115200);
  EXPECT_TRUE(absl::IsUnavailable(first.status())) << first.status();
  auto second = Owner::Open("/dev/null", 115200);
  EXPECT_TRUE(absl::IsUnavailable(second.status())) << second.status();
}

TEST_F(SerialIoOwnerTest, RepeatedCompletionCancelAndCloseRacesHaveNoStrandedWaiters) {
  for (int i = 0; i < 20; ++i) {
    auto op = owner->Submit(Request({}, 1));
    ASSERT_TRUE(Send({42}));
    auto cancel = std::async(std::launch::async, [op] { op.Cancel(); });
    auto close_owner = std::async(std::launch::async, [this] { return owner->Close(); });
    auto result = op.Wait();
    EXPECT_TRUE(result.status.ok() || absl::IsCancelled(result.status)) << result.status;
    cancel.get();
    ASSERT_TRUE(close_owner.get().ok());
    EXPECT_EQ(owner->Snapshot().pending_operations, 0);
    owner.reset();
    Open();
  }
}
}  // namespace
}  // namespace robot::comm
