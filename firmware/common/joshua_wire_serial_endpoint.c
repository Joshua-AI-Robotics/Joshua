// Wire envelope selection and firmware-session dispatch for neutral commands.
// Handlers consume/produce payloads only; v2 never builds an intermediate v1 frame.
#include "joshua_wire_serial_endpoint.h"

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
  const jw_command_t view = {
      request->cmd, request->channel, request->payload, request->payload_len};
  return command->handler(command->context, &view, payload, capacity);
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
    const jw_command_t view = {frame.cmd, frame.channel, frame.payload, frame.payload_len};
    uint8_t payload[JW1_MAX_PAYLOAD_LEN];
    const int len = handler(context, &view, payload, sizeof(payload));
    if (len < 0 || len > JW1_MAX_PAYLOAD_LEN) return -1;
    return jw1_encode_frame(
        response, response_cap, frame.cmd, frame.channel, payload, (uint8_t)len);
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
