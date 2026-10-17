// Native substitute for the single-core TI IRQ exclusion API.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
uintptr_t HwiP_disable(void);
void HwiP_restore(uintptr_t key);
#ifdef __cplusplus
}
#endif
