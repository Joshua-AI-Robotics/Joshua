// Wire envelope selection and firmware-session dispatch for neutral commands.
// Handlers consume/produce payloads only; v2 never builds an intermediate v1 frame.
#include "joshua_wire_serial_endpoint.h"

typedef struct {
  jw_serial_command_handler_t handler;
  jw_reset_handler_t reset;
  void* context;
} command_context_t;

static void reset_adapter(void* context) {
  command_context_t* command = (command_context_t*)context;
  command->reset(command->context);
}

static int command_adapter(void* context,
                           const jw_frame_t* request,
                           uint8_t* payload,
                           size_t capacity) {
  command_context_t* command = (command_context_t*)context;
  const jw_command_t view = {
      request->cmd, request->channel, request->payload, request->payload_len};
  return command->handler(command->context, &view, payload, capacity);
}

void jw_serial_endpoint_init(jw_serial_endpoint_t* endpoint) {
  if (endpoint == NULL) return;
  jw_firmware_session_init(&endpoint->session);
}

int jw_serial_endpoint_process(jw_serial_endpoint_t* endpoint,
                               const uint8_t* request,
                               size_t request_len,
                               uint8_t* response,
                               size_t response_cap,
                               jw_serial_command_handler_t handler,
                               jw_reset_handler_t reset,
                               void* context) {
  if (endpoint == NULL || handler == NULL || reset == NULL || response == NULL ||
      response_cap < JW_MAX_FRAME_LEN) {
    return -1;
  }
  command_context_t command = {handler, reset, context};
  return jw_firmware_session_process(&endpoint->session,
                                      request,
                                      request_len,
                                      response,
                                      response_cap,
                                      command_adapter,
                                      reset_adapter,
                                      &command);
}
