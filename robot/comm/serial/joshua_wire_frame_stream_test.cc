// PTYs only; continuous receive has no competing request-path reads.
#include "robot/comm/serial/joshua_wire_frame_stream.h"

#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

#include "firmware/common/joshua_wire_sensor_stream.h"
#include "gtest/gtest.h"

namespace robot::comm {
namespace {
using namespace std::chrono_literals;
using Clock = FrameStreamTransport::Clock;
using Bytes = std::vector<uint8_t>;
class FrameStreamTest : public ::testing::Test {
 protected:
  int master = -1;
  std::string path;
  std::shared_ptr<JoshuaWireFrameStream> link;
  void SetUp() override {
    master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    ASSERT_GE(master, 0);
    ASSERT_EQ(grantpt(master), 0);
    ASSERT_EQ(unlockpt(master), 0);
    path = ptsname(master);
    auto opened = JoshuaWireFrameStream::Open(path, 115200, 0ms, {}, {1000, 4, 2});
    ASSERT_TRUE(opened.ok()) << opened.status();
    link = *opened;
  }
  void TearDown() override {
    link.reset();
    if (master >= 0) close(master);
  }
  static Bytes Frame(uint32_t id, uint8_t cmd = JW_CMD_GET_STATUS) {
    Bytes bytes(JW_MAX_FRAME_LEN);
    const uint8_t reply[] = {JW_STATUS_OK, JW_EXECUTION_COMPLETE};
    const int size = jw_encode_frame(bytes.data(), bytes.size(), 10, id, cmd, 3, reply, 2);
    bytes.resize(size);
    return bytes;
  }
  static Bytes Sample(uint32_t token, uint32_t seq) {
    jw_stream_sample_t sample{};
    sample.session_id = 10;
    sample.subscription_id = token;
    sample.function_id = 3;
    sample.sample.value = seq;
    sample.sample.metadata = {JW_SAMPLE_VALID, JW_CLOCK_BOOT_MONOTONIC_US, 20, seq, seq};
    Bytes bytes(JW_STREAM_SAMPLE_FRAME_LEN);
    EXPECT_EQ(jw_encode_stream_sample(bytes.data(), bytes.size(), &sample), bytes.size());
    return bytes;
  }
  void Send(const Bytes& bytes) {
    ASSERT_EQ(write(master, bytes.data(), bytes.size()), static_cast<ssize_t>(bytes.size()));
  }
  bool WaitFrames(uint64_t count) {
    const auto end = Clock::now() + 1s;
    while (Clock::now() < end) {
      if (link->SnapshotFrames().received_frames >= count) return true;
      std::this_thread::sleep_for(1ms);
    }
    return false;
  }
};
TEST_F(FrameStreamTest, ReceivesWhileIdleAndKeepsReplyStorageSeparateFromFlood) {
  auto sample = Sample(7, 1);
  Send(sample);
  ASSERT_TRUE(WaitFrames(1));
  auto first = link->Receive(Clock::now() + 1s);
  ASSERT_TRUE(first.ok()) << first.status();
  EXPECT_EQ(first->bytes, sample);
  const auto receipt = first->received_at;
  for (uint32_t i = 0; i < 20; ++i) Send(Sample(7, i + 2));
  auto reply = Frame(7);  // Same numeric token: still a distinct reply kind.
  Send(reply);
  ASSERT_TRUE(WaitFrames(22));
  auto response = link->Receive(Clock::now() + 1s);
  ASSERT_TRUE(response.ok()) << response.status();
  EXPECT_EQ(response->bytes, reply);
  auto latest = link->Receive(Clock::now() + 1s);
  ASSERT_TRUE(latest.ok()) << latest.status();
  EXPECT_EQ(latest->bytes, Sample(7, 21));
  EXPECT_GE(latest->received_at, receipt);
  EXPECT_EQ(link->SnapshotFrames().notifications_dropped, 19);
  EXPECT_EQ(link->SnapshotFrames().notifications_pending, 0);
}
TEST_F(FrameStreamTest, NotificationCapacityDropsWholeFramesWithoutBlockingWrites) {
  Send(Sample(1, 1));
  Send(Sample(2, 2));
  Send(Sample(3, 3));
  ASSERT_TRUE(WaitFrames(3));
  auto diagnostics = link->SnapshotFrames();
  EXPECT_EQ(diagnostics.notifications_pending, 2);
  EXPECT_EQ(diagnostics.notifications_dropped, 1);
  auto request = Frame(40, JW_CMD_IDENTIFY);
  auto written = link->WriteFrame(request, Clock::now() + 1s);
  ASSERT_TRUE(written.status.ok()) << written.status;
  EXPECT_EQ(written.bytes_written, request.size());
  pollfd fd{master, POLLIN, 0};
  ASSERT_GT(poll(&fd, 1, 1000), 0);
  Bytes observed(request.size());
  ASSERT_EQ(read(master, observed.data(), observed.size()), observed.size());
  EXPECT_EQ(observed, request);
  auto second = link->Receive(Clock::now() + 1s);
  ASSERT_TRUE(second.ok()) << second.status();
  EXPECT_EQ(second->bytes, Sample(2, 2));
}
TEST_F(FrameStreamTest, CoalescingPreservesArrivalOrderForOldestNotificationEviction) {
  Send(Sample(1, 1));
  Send(Sample(2, 1));
  Send(Sample(1, 2));
  Send(Sample(3, 1));
  ASSERT_TRUE(WaitFrames(4));
  const auto diagnostics = link->SnapshotFrames();
  EXPECT_EQ(diagnostics.notifications_pending, 2);
  EXPECT_EQ(diagnostics.notifications_dropped, 2);
  auto newest_first_identity = link->Receive(Clock::now() + 1s);
  ASSERT_TRUE(newest_first_identity.ok()) << newest_first_identity.status();
  EXPECT_EQ(newest_first_identity->bytes, Sample(1, 2));
  auto newest_identity = link->Receive(Clock::now() + 1s);
  ASSERT_TRUE(newest_identity.ok()) << newest_identity.status();
  EXPECT_EQ(newest_identity->bytes, Sample(3, 1));
}

TEST_F(FrameStreamTest, FrameWritesProgressWhileTelemetryKeepsArriving) {
  std::promise<void> first_sample;
  auto producer = std::async(std::launch::async, [&] {
    for (uint32_t i = 1; i <= 100; ++i) {
      auto frame = Sample(7, i);
      size_t offset = 0;
      const auto deadline = Clock::now() + 1s;
      while (offset < frame.size() && Clock::now() < deadline) {
        pollfd descriptor{master, POLLOUT, 0};
        if (poll(&descriptor, 1, 1000) <= 0) break;
        const auto count = write(master, frame.data() + offset, frame.size() - offset);
        if (count > 0) offset += count;
      }
      if (i == 1) first_sample.set_value();
      if (offset != frame.size()) return false;
      std::this_thread::sleep_for(1ms);
    }
    return true;
  });
  first_sample.get_future().wait();
  for (uint32_t i = 1; i <= 10; ++i) {
    auto request = Frame(i, JW_CMD_IDENTIFY);
    auto written = link->WriteFrame(request, Clock::now() + 1s);
    EXPECT_TRUE(written.status.ok()) << written.status;
    EXPECT_EQ(written.bytes_written, request.size());
  }
  EXPECT_TRUE(producer.get());
  EXPECT_TRUE(WaitFrames(100));
  EXPECT_EQ(link->SnapshotFrames().notifications_pending, 1);
  EXPECT_EQ(link->SnapshotFrames().notifications_dropped, 99);
}

TEST_F(FrameStreamTest, MalformedAndFragmentedFramesResynchronize) {
  auto corrupted = Frame(1);
  corrupted.back() ^= 1;
  Send(corrupted);
  Send({JW_SYNC_BYTE, 0xff});
  auto valid = Frame(2);
  Send(Bytes(valid.begin(), valid.begin() + 3));
  std::this_thread::sleep_for(20ms);
  Send(Bytes(valid.begin() + 3, valid.end()));
  auto actual = link->Receive(Clock::now() + 1s);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(actual->bytes, valid);
  EXPECT_EQ(link->SnapshotFrames().malformed_frames, 2);
}
TEST_F(FrameStreamTest, IncompleteFrameExpiresAndNewFrameCanStart) {
  link.reset();
  auto opened = JoshuaWireFrameStream::Open(path, 115200, 0ms, {}, {100, 4, 2});
  ASSERT_TRUE(opened.ok()) << opened.status();
  link = std::move(*opened);
  Send({JW_SYNC_BYTE, 0x10, JW_PROTO_VERSION});
  const auto end = Clock::now() + 1s;
  while (Clock::now() < end && link->SnapshotFrames().assembling_bytes != 3)
    std::this_thread::sleep_for(1ms);
  ASSERT_EQ(link->SnapshotFrames().assembling_bytes, 3);
  std::this_thread::sleep_for(150ms);
  Send(Frame(4));
  auto actual = link->Receive(Clock::now() + 1s);
  ASSERT_TRUE(actual.ok()) << actual.status();
  EXPECT_EQ(actual->bytes, Frame(4));
  EXPECT_EQ(link->SnapshotFrames().partial_frames_expired, 1);
}
TEST_F(FrameStreamTest, ReplyOverflowFaultsAndRetiresPhysicalOwner) {
  for (uint32_t i = 1; i <= 5; ++i) Send(Frame(i));
  auto result = std::async(std::launch::async, [&] {
    const auto end = Clock::now() + 1s;
    while (Clock::now() < end && link->status().ok()) std::this_thread::sleep_for(1ms);
    return link->status();
  });
  EXPECT_TRUE(absl::IsResourceExhausted(result.get()));
  EXPECT_FALSE(link->Receive(Clock::now() + 1s).ok());
  EXPECT_EQ(link->SnapshotFrames().reply_overflows, 1);
  EXPECT_FALSE(link->WriteFrame(Frame(6), Clock::now() + 1s).status.ok());
}
TEST_F(FrameStreamTest, WakeCloseAndDisconnectUnblockReceivers) {
  link->Interrupt();
  EXPECT_TRUE(absl::IsCancelled(link->Receive(Clock::now() + 30s).status()));
  auto waiting = std::async(std::launch::async, [&] { return link->Receive(Clock::now() + 30s); });
  ASSERT_TRUE(link->Close().ok());
  EXPECT_TRUE(absl::IsCancelled(waiting.get().status()));
  EXPECT_TRUE(link->Close().ok());
  link.reset();
  auto opened = JoshuaWireFrameStream::Open(path, 115200, 0ms, {}, {});
  ASSERT_TRUE(opened.ok()) << opened.status();
  link = *opened;
  auto disconnected =
      std::async(std::launch::async, [&] { return link->Receive(Clock::now() + 30s); });
  close(master);
  master = -1;
  EXPECT_TRUE(absl::IsUnavailable(disconnected.get().status()));
}
TEST_F(FrameStreamTest, InvalidOrExpiredFramesNeverPublish) {
  EXPECT_TRUE(absl::IsInvalidArgument(link->WriteFrame({}, Clock::now() + 1s).status));
  EXPECT_TRUE(absl::IsInvalidArgument(link->WriteFrame(Sample(7, 1), Clock::now() + 1s).status));
  EXPECT_TRUE(absl::IsDeadlineExceeded(link->WriteFrame(Frame(1), Clock::now() - 1ms).status));
  pollfd fd{master, POLLIN, 0};
  EXPECT_EQ(poll(&fd, 1, 10), 0);
}
}  // namespace
}  // namespace robot::comm
