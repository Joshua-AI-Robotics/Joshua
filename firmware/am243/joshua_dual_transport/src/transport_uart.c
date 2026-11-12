#include "transport_uart.h"

#include <drivers/uart.h>
#include <kernel/dpl/ClockP.h>
static int ReadByte(void* context) {
  JoshuaUartTransport* state = (JoshuaUartTransport*)context;
  uint8_t byte;
  return UART_getChar(state->base_address, &byte) ? byte : -1;
}
static size_t WriteBytes(void* context, const uint8_t* bytes, size_t length) {
  JoshuaUartTransport* state = (JoshuaUartTransport*)context;
  if (!(UART_readLineStatus(state->base_address) & UART_LSR_TX_FIFO_E_MASK)) return 0;
  // An empty AM243 UART FIFO holds 64 bytes; revision-4 frames can be 96.
  // The shared adapter retains the remainder until the FIFO drains.
  const size_t count = length < 64u ? length : 64u;
  for (size_t i = 0; i < count; ++i) UART_putChar(state->base_address, bytes[i]);
  return count;
}
static uint32_t NowMs(void* context) {
  (void)context;
  return (uint32_t)(ClockP_getTimeUsec() / 1000U);
}
frame_transport_t JoshuaUartTransportInit(JoshuaUartTransport* state,
                                          uint32_t base_address,
                                          uint32_t byte_timeout_ms) {
  state->base_address = base_address;
  UART_intrDisable(base_address, UART_INTR_RHR_CTI | UART_INTR_THR | UART_INTR_LINE_STAT);
  const serial_frame_transport_config_t config = {
      state, ReadByte, WriteBytes, NowMs, byte_timeout_ms};
  return serial_frame_transport_init(&state->serial, &config);
}
