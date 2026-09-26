// Migration adapter for firmware command handlers using v1 payload helpers.
// Wire version is explicit per artifact; it is never auto-detected/downgraded.
// Declares the endpoint and callbacks that connect raw frames to board command
// handlers, using joshua_wire_v2_firmware_session for v2 session state. Owns no UART I/O.
// TODO(payload migration): Remove the in-memory v1 frame bridge when command
// handlers use neutral payload views/encoders. See README.md, migration adapters.
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

// A handler consumes the unchanged v1 command payload and encodes a v1 reply.
// The adapter validates that reply and emits its payload with v2 correlation.
// The v1 view is in-memory only; v2 bytes never enter the v1 frame decoder.
typedef int (*jw_serial_command_handler_t)(void* context,
                                           const jw1_frame_t* command,
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
