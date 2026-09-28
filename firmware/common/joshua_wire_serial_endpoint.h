// Firmware endpoint for explicitly selected v1/v2 artifacts. Decodes the wire
// envelope and dispatches a neutral command view; handlers return payload bytes.
// Framing and v2 session/correlation live here, not in drive command handlers.
// Owns no UART I/O and never auto-detects or downgrades the wire version.
#pragma once

#include "joshua_wire_v1.h"
#include "joshua_wire_v2_firmware_session.h"

#ifndef JOSHUA_WIRE_VERSION
#define JOSHUA_WIRE_VERSION 1
#endif
#if JOSHUA_WIRE_VERSION != 1 && JOSHUA_WIRE_VERSION != 2
#error "JOSHUA_WIRE_VERSION must be 1 or 2"
#endif

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
  uint8_t wire_version;
  jw2_firmware_session_t session;
} jw_serial_endpoint_t;

void jw_serial_endpoint_init(jw_serial_endpoint_t* endpoint, uint8_t wire_version);
int jw_serial_endpoint_process(jw_serial_endpoint_t* endpoint,
                               const uint8_t* request,
                               size_t request_len,
                               uint8_t* response,
                               size_t response_cap,
                               jw_serial_command_handler_t handler,
                               jw2_reset_handler_t reset,
                               void* context);

#ifdef __cplusplus
}
#endif
