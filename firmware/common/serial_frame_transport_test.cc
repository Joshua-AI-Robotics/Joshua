#include "firmware/common/serial_frame_transport.h"

#include <algorithm>
#include <vector>

#include "gtest/gtest.h"

namespace {
struct Link {
  size_t allowance = 0;
  std::vector<uint8_t> output;
};
int ReadByte(void*) {
  return -1;
}
size_t WriteBytes(void* context, const uint8_t* bytes, size_t length) {
  auto* link = static_cast<Link*>(context);
  const size_t count = std::min(length, link->allowance);
  link->output.insert(link->output.end(), bytes, bytes + count);
  link->allowance -= count;
  return count;
}
uint32_t NowMs(void*) {
  return 0;
}

TEST(SerialFrameTransportTest, CopiesAcceptedFrameAndRetainsShortWritesWithoutInterleaving) {
  Link link;
  serial_frame_transport_t state;
  const serial_frame_transport_config_t config = {&link, ReadByte, WriteBytes, NowMs, 20};
  const auto transport = serial_frame_transport_init(&state, &config);
  uint8_t first[] = {1, 2, 3, 4, 5};
  const uint8_t second[] = {6, 7, 8};
  EXPECT_EQ(transport.try_send(transport.context, first, sizeof(first)), FRAME_OK);
  first[0] = 99;
  EXPECT_EQ(transport.try_send(transport.context, second, sizeof(second)), FRAME_WOULD_BLOCK);
  EXPECT_TRUE(link.output.empty());
  uint8_t received[JW_MAX_FRAME_LEN];
  size_t length = 99;
  link.allowance = 2;
  EXPECT_EQ(transport.poll_receive(transport.context, received, sizeof(received), &length),
            FRAME_NO_DATA);
  EXPECT_EQ(length, 0);
  EXPECT_EQ(link.output, (std::vector<uint8_t>{1, 2}));
  EXPECT_EQ(transport.try_send(transport.context, second, sizeof(second)), FRAME_WOULD_BLOCK);
  link.allowance = 3;
  EXPECT_EQ(transport.try_send(transport.context, second, sizeof(second)), FRAME_OK);
  EXPECT_EQ(link.output, (std::vector<uint8_t>{1, 2, 3, 4, 5}));
  EXPECT_EQ(state.transmit_length, sizeof(second));
  link.allowance = 3;
  EXPECT_EQ(transport.poll_receive(transport.context, received, sizeof(received), &length),
            FRAME_NO_DATA);
  EXPECT_EQ(link.output, (std::vector<uint8_t>{1, 2, 3, 4, 5, 6, 7, 8}));
  EXPECT_EQ(state.transmit_length, 0);
}
}  // namespace
