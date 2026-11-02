// Native subset of TI SDK crypto engine control, implemented by firmware tests.
#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
int32_t SA2UL_engineEnable(int32_t mask);
int32_t SA2UL_engineDisable(int32_t mask);
#ifdef __cplusplus
}
#endif
