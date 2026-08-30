#pragma once

#include <stdarg.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @file joshua_serial_task.h
 * @brief Helpers for the Joshua serial transport task.
 *
 * JoshuaSerialStart() starts the serial TaskP thread that handles incoming and
 * outgoing `joshua_wire_v1` frames. Return value convention:
 *   - 0 on success
 *   - negative error code on failure (e.g. thread creation failure)
 *
 * JoshuaSerialDisableLogs() silences kernel/SDK textual logs so they do not
 * corrupt the binary serial protocol stream.
 *
 * JoshuaSerialDiscardLog() is a vprintf-style no-op print callback suitable
 * for registering with SDK/OSAL print facilities when the serial protocol is
 * active.
 *
 * Thread-safety: call JoshuaSerialStart() once during system initialization.
 */

int32_t JoshuaSerialStart(void);
void JoshuaSerialDisableLogs(void);
void JoshuaSerialDiscardLog(void* context, const char* format, va_list args);

#ifdef __cplusplus
}
#endif
