// Test-only clock declaration for the native AM243 UART adapter test.
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
uint64_t ClockP_getTimeUsec(void);
void ClockP_usleep(uint32_t usec);
#ifdef __cplusplus
}
#endif
