#pragma once

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int32_t JoshuaSerialStart(void);
void JoshuaSerialDisableLogs(void);
void JoshuaSerialDiscardLog(void* context, const char* format, va_list args);

#ifdef __cplusplus
}
#endif
