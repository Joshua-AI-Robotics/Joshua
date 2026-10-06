#include "transport_uart.h"

#include <drivers/uart.h>
#include <kernel/dpl/ClockP.h>

#include <deque>
#include <vector>

#include "gtest/gtest.h"

namespace {
std::deque<uint8_t> input;
std::vector<uint8_t> output;
bool fifo_empty;
uint32_t disabled_interrupts;
uint64_t clock_us;
constexpr uint32_t kBase = 0x1234;

class UartTransportTest : public ::testing::Test {
 protected:
  JoshuaUartTransport state;
  frame_transport_t transport;
  void SetUp() override {
    input.clear();
    output.clear();
    fifo_empty = true;
    clock_us = 0;
    disabled_interrupts = 0;
    transport = JoshuaUartTransportInit(&state, kBase, 20);
  }
};

TEST_F(UartTransportTest, AcceptsWholeFrameOnlyWhenFifoIsEmpty) {
  EXPECT_EQ(disabled_interrupts, UART_INTR_RHR_CTI | UART_INTR_THR | UART_INTR_LINE_STAT);
  uint8_t frame[JW_MAX_FRAME_LEN] = {JW_SYNC_BYTE};
  fifo_empty = false;
  EXPECT_EQ(transport.try_send(transport.context, frame, sizeof(frame)), FRAME_WOULD_BLOCK);
  EXPECT_TRUE(output.empty());
  fifo_empty = true;
  EXPECT_EQ(transport.try_send(transport.context, frame, sizeof(frame)), FRAME_OK);
  EXPECT_EQ(output.size(), sizeof(frame));
  frame[0] = 0;
  EXPECT_EQ(output[0], JW_SYNC_BYTE);  // Accepted storage belongs to the adapter/FIFO.
  EXPECT_EQ(transport.try_send(transport.context, frame, sizeof(frame) + 1), FRAME_ERROR);
  EXPECT_EQ(output.size(), sizeof(frame));
}

TEST_F(UartTransportTest, RetainsPartialReceiveAndBoundsNoiseDraining) {
  uint8_t frame[JW_MAX_FRAME_LEN];
  const int encoded =
      jw_encode_frame(frame, sizeof(frame), 10, 1, JW_CMD_IDENTIFY, 0xff, nullptr, 0);
  input.insert(input.end(), frame, frame + 5);
  uint8_t received[JW_MAX_FRAME_LEN];
  size_t length = 99;
  EXPECT_EQ(transport.poll_receive(transport.context, received, sizeof(received), &length),
            FRAME_NO_DATA);
  EXPECT_EQ(length, 0);
  clock_us += 1000;
  input.insert(input.end(), frame + 5, frame + encoded);
  ASSERT_EQ(transport.poll_receive(transport.context, received, sizeof(received), &length),
            FRAME_OK);
  EXPECT_EQ(std::vector<uint8_t>(received, received + length),
            std::vector<uint8_t>(frame, frame + encoded));
  input.insert(input.end(), JW_MAX_FRAME_LEN * 3, 0);
  EXPECT_EQ(transport.poll_receive(transport.context, received, sizeof(received), &length),
            FRAME_NO_DATA);
  EXPECT_EQ(input.size(), JW_MAX_FRAME_LEN * 2);
  EXPECT_EQ(length, 0);
}
}  // namespace

extern "C" uint32_t UART_getChar(uint32_t base, uint8_t* byte) {
  EXPECT_EQ(base, kBase);
  if (input.empty()) return 0;
  *byte = input.front();
  input.pop_front();
  return 1;
}
extern "C" void UART_putChar(uint32_t base, uint8_t byte) {
  EXPECT_EQ(base, kBase);
  output.push_back(byte);
}
extern "C" uint32_t UART_readLineStatus(uint32_t base) {
  EXPECT_EQ(base, kBase);
  return fifo_empty ? UART_LSR_TX_FIFO_E_MASK : 0;
}
extern "C" void UART_intrDisable(uint32_t base, uint32_t flags) {
  EXPECT_EQ(base, kBase);
  disabled_interrupts = flags;
}
extern "C" uint64_t ClockP_getTimeUsec(void) {
  return clock_us;
}
