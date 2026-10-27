// Test-only declarations for the FIFO APIs used by the AM243 UART adapter.
// MCU builds use TI's real SDK, never this include directory.
#pragma once

#include <stdint.h>

#define UART_LSR_TX_FIFO_E_MASK (1U << 5)
#define UART_INTR_RHR_CTI (1U << 0)
#define UART_INTR_THR (1U << 1)
#define UART_INTR_LINE_STAT (1U << 2)

#ifdef __cplusplus
extern "C" {
#endif
uint32_t UART_getChar(uint32_t base_address, uint8_t* byte);
void UART_putChar(uint32_t base_address, uint8_t byte);
uint32_t UART_readLineStatus(uint32_t base_address);
void UART_intrDisable(uint32_t base_address, uint32_t flags);
#ifdef __cplusplus
}
#endif
