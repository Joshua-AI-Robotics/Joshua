// Exclusive, polled FIFO binding for the UART profile; never calls blocking I/O.
#pragma once

#include "serial_frame_assembler.h"

typedef struct {
  uint32_t base_address;
  serial_frame_assembler_t assembler;
} JoshuaUartTransport;

// Startup supplies the already configured UART base address and link timeout.
// No SDK UART transactions/log writers may use this UART after initialization.
frame_transport_t JoshuaUartTransportInit(JoshuaUartTransport* state,
                                          uint32_t base_address,
                                          uint32_t byte_timeout_ms);
