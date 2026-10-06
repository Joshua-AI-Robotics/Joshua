// Firmware endpoint for JoshuaWire 0.0.2. Decodes the wire
// envelope and dispatches a neutral command view; handlers return payload bytes.
// Framing and session/correlation live here, not in drive command handlers.
// Owns no UART I/O and never auto-detects or downgrades the wire version.
#pragma once

#include "joshua_wire_firmware_session.h"

#ifdef __cplusplus
extern "C" {
#endif

// Handler returns a response payload byte count, or -1 on failure. The command
// and its payload are borrowed for this call only. The endpoint owns framing.
typedef int (*jw_serial_command_handler_t)(void* context,
                                           const jw_command_t* command,
                                           uint8_t* response,
                                           size_t capacity);
typedef struct {
  jw_firmware_session_t session;
} jw_serial_endpoint_t;

void jw_serial_endpoint_init(jw_serial_endpoint_t* endpoint);
int jw_serial_endpoint_process(jw_serial_endpoint_t* endpoint,
                               const uint8_t* request,
                               size_t request_len,
                               uint8_t* response,
                               size_t response_cap,
                               jw_serial_command_handler_t handler,
                               jw_reset_handler_t reset,
                               void* context);

#ifdef __cplusplus
}
#endif
