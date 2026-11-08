// Production Feetech board + CommFactory + serial owner against an allocated PTY.
// No physical serial device is opened.
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "robot/board/feetech_bus/feetech_bus_board.h"
#include "robot/board/feetech_bus/feetech_protocol.h"
#include "robot/comm/factory/comm_factory.h"

namespace robot::comm {
namespace {
using namespace std::chrono_literals;
using Bytes = std::vector<uint8_t>;

class FeetechSerialTest : public testing::Test {
 protected:
  void SetUp() override {
    master_ = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
    ASSERT_GE(master_, 0);
    ASSERT_EQ(grantpt(master_), 0);
    ASSERT_EQ(unlockpt(master_), 0);
    // Keep a slave descriptor open so the emulator can start before board Init.
    slave_ = open(ptsname(master_), O_RDWR | O_NOCTTY);
    ASSERT_GE(slave_, 0);
    config_.set_name("pty_servos");
    config_.set_board_type(robot::board::FEETECH_BUS);
    auto* comm = config_.mutable_comm();
    comm->set_comm_type(SERIAL);
    comm->set_transport_type(MESSAGE);
    auto* serial = comm->mutable_serial_config();
    serial->set_port(ptsname(master_));
    serial->set_baudrate(1000000);
    serial->set_message_adapter(CALLER_SIZED);
    serial->mutable_io_policy()->set_transaction_timeout_ms(500);
    for (int id : {5, 6}) {
      auto* channel = config_.add_channels();
      channel->set_index(id);
      channel->set_drive(robot::board::SERVO_BUS_UART);
      channel->mutable_servo_bus()->set_servo_id(id);
    }
    emulator_ = std::thread([this] { RunServo(); });
  }
  void TearDown() override {
    stopping_ = true;
    if (emulator_.joinable()) emulator_.join();
    board_.Teardown().IgnoreError();
    if (slave_ >= 0) close(slave_);
    if (master_ >= 0) close(master_);
    EXPECT_FALSE(device_error_);
    CommFactory::ResetSerialTransportCacheForTesting();
  }
  bool ReadBytes(Bytes& bytes, size_t size) {
    while (bytes.size() < size && !stopping_) {
      pollfd fd{master_, POLLIN, 0};
      if (poll(&fd, 1, 10) <= 0) continue;
      uint8_t buffer[64];
      const auto n = read(master_, buffer, std::min(sizeof(buffer), size - bytes.size()));
      if (n <= 0) return false;
      bytes.insert(bytes.end(), buffer, buffer + n);
    }
    return bytes.size() == size;
  }
  void Reply(uint8_t id, const Bytes& params) {
    Bytes reply{255, 255, id, static_cast<uint8_t>(params.size() + 2), 0};
    reply.insert(reply.end(), params.begin(), params.end());
    unsigned sum = 0;
    for (size_t i = 2; i < reply.size(); ++i) sum += reply[i];
    reply.push_back(static_cast<uint8_t>(~sum));
    // Fragment replies, including the six-byte write acknowledgment.
    if (write(master_, reply.data(), 2) != 2) device_error_ = true;
    std::this_thread::sleep_for(1ms);
    if (write(master_, reply.data() + 2, reply.size() - 2) !=
        static_cast<ssize_t>(reply.size() - 2))
      device_error_ = true;
  }
  void RunServo() {
    while (!stopping_) {
      Bytes request;
      if (!ReadBytes(request, 4)) return;
      if (request[0] != 255 || request[1] != 255 || request[3] < 2 || request[3] > 32 ||
          !ReadBytes(request, request[3] + 4)) {
        device_error_ = true;
        return;
      }
      const auto id = request[2];
      if (request[4] == 1) {
        Reply(id, {});
      } else if (request[4] == 2) {
        switch (request[5]) {
          case robot::board::feetech::kRegModelNumber:
            Reply(id, {9, 3});
            break;
          case robot::board::feetech::kRegStatusReturnLevel:
            Reply(id, {static_cast<uint8_t>(id == 5)});
            break;
          case robot::board::feetech::kRegPresentPosition:
            Reply(id, {0x16, 0x08});
            break;
          default:
            device_error_ = true;
            return;
        }
      } else if (request[4] == 3) {
        if (id == 5) {
          ++acknowledged_writes_;
          while (hold_first_reply_ && acknowledged_writes_ == 1 && !release_first_reply_ &&
                 !stopping_)
            std::this_thread::sleep_for(1ms);
          // Observe the original failure: another request must not be sent
          // while this write's delayed reply is still outstanding.
          pollfd fd{master_, POLLIN, 0};
          if (!hold_first_reply_ && poll(&fd, 1, 30) > 0) request_before_ack_ = true;
          Reply(id, {});
          while (hold_first_reply_ && acknowledged_writes_ == 1 && !resume_device_ && !stopping_)
            std::this_thread::sleep_for(1ms);
        } else {
          ++silent_writes_;
        }
      } else {
        device_error_ = true;
        return;
      }
    }
  }

  robot::board::Board config_;
  robot::board::FeetechBusBoard board_;
  int master_ = -1, slave_ = -1;
  std::thread emulator_;
  std::atomic<bool> stopping_{false}, device_error_{false}, request_before_ack_{false};
  std::atomic<int> acknowledged_writes_{0}, silent_writes_{0};
  std::atomic<bool> hold_first_reply_{false}, release_first_reply_{false}, resume_device_{false};
};

TEST_F(FeetechSerialTest, LateEnableReplyCannotConfirmDisableOrReviveAnyChannel) {
  config_.mutable_comm()->mutable_serial_config()->mutable_io_policy()->set_transaction_timeout_ms(
      100);
  hold_first_reply_ = true;
  ASSERT_TRUE(board_.Init(config_).ok());
  auto channel = board_.OpenChannel(5);
  auto other = board_.OpenChannel(6);
  ASSERT_TRUE(channel.ok());
  ASSERT_TRUE(other.ok());
  auto enabled = (*channel)->Enable();
  EXPECT_EQ(enabled.code(), absl::StatusCode::kDeadlineExceeded);
  ASSERT_EQ(acknowledged_writes_, 1);

  auto disabling = std::async(std::launch::async, [&] { return (*channel)->Disable(); });
  // The emulator holds the enable ACK until the disable request has reached
  // the wire. Then it sends only the old ACK, without processing disable.
  pollfd fd{master_, POLLIN, 0};
  EXPECT_GT(poll(&fd, 1, 1000), 0);
  release_first_reply_ = true;
  auto disabled = disabling.get();
  EXPECT_EQ(disabled.code(), absl::StatusCode::kFailedPrecondition)
      << "A late enable ACK must never confirm disable";
  EXPECT_NE(disabled.message().find("unconfirmed"), std::string::npos);
  EXPECT_EQ(acknowledged_writes_, 1);

  // A complete late reply cannot restore alignment or bypass the shared fault,
  // including on a different channel configured for send-only writes.
  EXPECT_EQ((*channel)->ReadFeedback().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ((*other)->ReadFeedback().status().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ((*other)->Enable().code(), absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ((*other)->SetTarget(robot::board::TargetMode::kPosition, 2070).code(),
            absl::StatusCode::kFailedPrecondition);
  EXPECT_EQ((*other)->Disable().code(), absl::StatusCode::kFailedPrecondition);
  resume_device_ = true;
}

TEST_F(FeetechSerialTest, DelayedWriteRepliesCannotContaminateAnotherChannelsFeedback) {
  ASSERT_TRUE(board_.Init(config_).ok());
  auto writer = board_.OpenChannel(5);
  auto reader = board_.OpenChannel(6);
  ASSERT_TRUE(writer.ok());
  ASSERT_TRUE(reader.ok());
  // Cover both register-write paths; speed is staged locally until position.
  ASSERT_TRUE((*writer)->SetTarget(robot::board::TargetMode::kVelocity, 100).ok());
  for (int i = 0; i < 3; ++i) {
    auto command = std::async(std::launch::async, [&, i] {
      if (i == 0) return (*writer)->Disable();
      if (i == 1) return (*writer)->Enable();
      return (*writer)->SetTarget(robot::board::TargetMode::kPosition, 2070);
    });
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (acknowledged_writes_ <= i && std::chrono::steady_clock::now() < deadline)
      std::this_thread::sleep_for(1ms);
    EXPECT_EQ(acknowledged_writes_, i + 1);
    auto feedback = std::async(std::launch::async, [&] { return (*reader)->ReadFeedback(); });
    EXPECT_TRUE(command.get().ok());
    auto sample = feedback.get();
    ASSERT_TRUE(sample.ok()) << sample.status();
    EXPECT_EQ(sample->position, 2070);
    EXPECT_FALSE(request_before_ack_);
  }
  // A servo configured for read-only replies must not wait for a write ACK.
  EXPECT_TRUE((*reader)->Disable().ok());
  auto sample = (*writer)->ReadFeedback();
  ASSERT_TRUE(sample.ok()) << sample.status();
  EXPECT_EQ(sample->position, 2070);
  EXPECT_EQ(silent_writes_, 1);
}
}  // namespace
}  // namespace robot::comm
