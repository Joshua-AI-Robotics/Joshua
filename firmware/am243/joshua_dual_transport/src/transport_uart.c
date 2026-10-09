#include "transport_uart.h"

#include <drivers/uart.h>
#include <kernel/dpl/ClockP.h>

static frame_status_t PollReceive(void* context, uint8_t* frame, size_t capacity, size_t* length) {
  if (length == NULL) return FRAME_ERROR;
  *length = 0;
  if (frame == NULL || capacity < JW_MAX_FRAME_LEN) return FRAME_ERROR;
  JoshuaUartTransport* state = (JoshuaUartTransport*)context;
  for (size_t budget = 0; budget < JW_MAX_FRAME_LEN; ++budget) {
    uint8_t byte;
    if (!UART_getChar(state->base_address, &byte)) break;
    const frame_status_t status = serial_frame_assembler_push(
        &state->assembler, (uint32_t)(ClockP_getTimeUsec() / 1000U), byte, frame, capacity, length);
    if (status != FRAME_NO_DATA) return status;
  }
  return FRAME_NO_DATA;
}

static frame_status_t TrySend(void* context, const uint8_t* frame, size_t length) {
  if (frame == NULL || length == 0 || length > JW_MAX_FRAME_LEN) return FRAME_ERROR;
  const JoshuaUartTransport* state = (const JoshuaUartTransport*)context;
  // AM243's enabled UART FIFO holds 64 bytes. Only write when it is empty:
  // the entire JW frame fits, and this adapter is its exclusive writer.
  if ((UART_readLineStatus(state->base_address) & UART_LSR_TX_FIFO_E_MASK) == 0) {
    return FRAME_WOULD_BLOCK;
  }
  for (size_t i = 0; i < length; ++i) UART_putChar(state->base_address, frame[i]);
  return FRAME_OK;
}

frame_transport_t JoshuaUartTransportInit(JoshuaUartTransport* state,
                                          uint32_t base_address,
                                          uint32_t byte_timeout_ms) {
  state->base_address = base_address;
  serial_frame_assembler_init(&state->assembler, byte_timeout_ms);
  // No interrupt-driven UART transaction shares this FIFO with the adapter.
  UART_intrDisable(base_address, UART_INTR_RHR_CTI | UART_INTR_THR | UART_INTR_LINE_STAT);
  const frame_transport_t transport = {state, PollReceive, TrySend};
  return transport;
}
