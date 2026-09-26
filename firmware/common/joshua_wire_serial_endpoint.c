// Implements the firmware migration bridge between explicit v1/v2 wire
// artifacts and existing v1 command-payload helpers. V2 uses the firmware session
// and rewraps handler replies with correlation IDs; v2 wire bytes are never
// passed to the v1 decoder. This file owns no serial hardware I/O.
#include "joshua_wire_serial_endpoint.h"

#include <string.h>

typedef struct {
  jw_serial_command_handler_t handler;
  jw2_reset_handler_t reset;
  void* context;
} command_context_t;

static void reset_adapter(void* context) {
  command_context_t* command = (command_context_t*)context;
  command->reset(command->context);
}

static int command_adapter(void* context,
                           const jw2_frame_t* request,
                           uint8_t* payload,
                           size_t capacity) {
  command_context_t* command = (command_context_t*)context;
  jw1_frame_t view;
  view.proto_ver = JW1_PROTO_VERSION;
  view.cmd = request->cmd;
  view.channel = request->channel;
  view.payload = request->payload;
  view.payload_len = request->payload_len;
  uint8_t response[JW1_MAX_FRAME_LEN];
  const int len = command->handler(command->context, &view, response, sizeof(response));
  jw1_frame_t decoded;
  if (len <= 0 || len > JW1_MAX_FRAME_LEN ||
      jw1_decode_frame(response, (size_t)len, &decoded) != 0 || decoded.cmd != request->cmd ||
      decoded.channel != request->channel || decoded.payload_len > capacity) {
    return -1;
  }
  memcpy(payload, decoded.payload, decoded.payload_len);
  return decoded.payload_len;
}

void jw_serial_endpoint_init(jw_serial_endpoint_t* endpoint, uint8_t wire_version) {
  if (endpoint == NULL) return;
  endpoint->wire_version = wire_version;
  jw2_firmware_session_init(&endpoint->session);
}

int jw_serial_endpoint_process(jw_serial_endpoint_t* endpoint,
                               const uint8_t* request,
                               size_t request_len,
                               uint8_t* response,
                               size_t response_cap,
                               jw_serial_command_handler_t handler,
                               jw2_reset_handler_t reset,
                               void* context) {
  if (endpoint == NULL || handler == NULL || reset == NULL || response == NULL ||
      response_cap < JW2_MAX_FRAME_LEN) {
    return -1;
  }
  if (endpoint->wire_version == JW1_PROTO_VERSION) {
    jw1_frame_t frame;
    if (jw1_decode_frame(request, request_len, &frame) != 0) return 0;
    return handler(context, &frame, response, response_cap);
  }
  if (endpoint->wire_version != JW2_PROTO_VERSION) return -1;
  command_context_t command = {handler, reset, context};
  return jw2_firmware_session_process(&endpoint->session,
                                      request,
                                      request_len,
                                      response,
                                      response_cap,
                                      command_adapter,
                                      reset_adapter,
                                      &command);
}
