// Ordered, single-flight firmware session endpoint for JoshuaWire.
// Declares active-session/request history, cached replies and board callbacks.
// Remembers requests to prevent re-execution; owns no I/O or worker thread.
// Call from one dispatch loop (or externally serialize the entire call). PDO
// generations, cross-plane arbitration and watchdogs belong to the EtherCAT
// profile; the session alone does not provide those guarantees.
#pragma once

#include "joshua_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint32_t session_id;
  uint32_t last_message_id;
  uint8_t last_request[JW_MAX_FRAME_LEN];
  uint8_t last_request_len;
  uint8_t response[JW_MAX_FRAME_LEN];
  uint8_t response_len;
} jw_firmware_session_t;

// Handler produces a command response payload, including error status payloads,
// and returns its length or -1. It must not write beyond capacity. reset must
// disable all outputs and invalidate channel configuration before returning.
typedef int (*jw_command_handler_t)(void* context,
                                    const jw_frame_t* request,
                                    uint8_t* payload,
                                    size_t capacity);
typedef void (*jw_reset_handler_t)(void* context);

void jw_firmware_session_init(jw_firmware_session_t* session);

// Returns response length, 0 for a discarded frame, or -1 for API/handler errors.
// New sessions require RESET_SESSION (channel=0xff, empty payload). Its response
// is one status byte (JW_STATUS_OK). A duplicate of the most recent identical request
// replays the retained response; older IDs and changed requests reusing an ID
// are discarded. An ID is consumed even if its handler fails. Input/output
// buffers must not overlap the session or each other; output capacity must be 64.
// A different-session reset is accepted by contract: session IDs provide
// correlation, not authentication or protection against injected resets.
int jw_firmware_session_process(jw_firmware_session_t* session,
                                const uint8_t* request,
                                size_t request_len,
                                uint8_t* response,
                                size_t response_cap,
                                jw_command_handler_t handler,
                                jw_reset_handler_t reset,
                                void* context);

#ifdef __cplusplus
}
#endif
