#include "robot/board/frame/serial_frame_transport.h"

#include <memory>

#include "gtest/gtest.h"
#include "robot/comm/serial/fake_serial_transport.h"

namespace robot::board {
namespace {

TEST(SerialFrameTransportTest, ForwardsRequestAndReturnsQueuedResponse) {
  auto fake = std::make_shared<robot::comm::FakeSerialTransport>();
  fake->QueueResponse({0xa5, 0x03, 0x01, 0x05, 0x00, 0x00, 0x00});
  SerialFrameTransport transport(fake);

  std::vector<uint8_t> request = {0xa5, 0x03, 0x01, 0x05, 0x02, 0x1b, 0x24};
  auto result = transport.SendAndReceive(request, 7);

  ASSERT_TRUE(result.ok()) << result.status();
  EXPECT_EQ(*result, (std::vector<uint8_t>{0xa5, 0x03, 0x01, 0x05, 0x00, 0x00, 0x00}));
  EXPECT_EQ(fake->atomic_read_calls_, 1);
  EXPECT_EQ(fake->last_written_, request);
}

TEST(SerialFrameTransportTest, SurfacesTransportFailure) {
  auto fake = std::make_shared<robot::comm::FakeSerialTransport>();
  fake->atomic_read_status_ = absl::UnavailableError("no ack");
  SerialFrameTransport transport(fake);

  auto result = transport.SendAndReceive({0xa5, 0x03, 0x01, 0x05, 0x02, 0x1b, 0x24}, 7);

  EXPECT_EQ(result.status().code(), absl::StatusCode::kUnavailable);
}

}  // namespace
}  // namespace robot::board
