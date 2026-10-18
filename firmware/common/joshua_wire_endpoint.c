// Wire envelope selection and firmware-session dispatch for neutral commands.
// Handlers consume/produce payloads only; the session owns the correlated envelope.
#include "joshua_wire_endpoint.h"

typedef struct {
  jw_command_handler_fn handler;
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
  int valid_scope = 0;
  switch (request->cmd) {
    case JW_CMD_IDENTIFY:
    case JW_CMD_DISCOVER:
    case JW_CMD_ESTOP:
    case JW_CMD_GET_DEPENDENCIES:
    case JW_CMD_RECOVER:
      valid_scope = request->channel == JW_CHANNEL_NONE;
      break;
    case JW_CMD_GET_STATUS:
      valid_scope = 1;
      break;
    case JW_CMD_PROFILE:
    case JW_CMD_DESCRIBE_FUNCTION:
      valid_scope = request->channel != JW_CHANNEL_NONE;
      break;
    default:
      return jw_encode_reply_payload(
          payload, capacity, JW_STATUS_UNSUPPORTED, JW_EXECUTION_REJECTED, NULL, 0);
  }
  if (!valid_scope)
    return jw_encode_reply_payload(
        payload, capacity, JW_STATUS_INVALID_CHANNEL, JW_EXECUTION_REJECTED, NULL, 0);
  const jw_command_t view = {request->session_id,
                             request->message_id,
                             request->cmd,
                             request->channel,
                             request->payload,
                             request->payload_len};
  return command->handler(command->context, &view, payload, capacity);
}

void jw_endpoint_init(jw_endpoint_t* endpoint) {
  if (endpoint == NULL) return;
  jw_firmware_session_init(&endpoint->session);
}

int jw_endpoint_process(jw_endpoint_t* endpoint,
                        const uint8_t* request,
                        size_t request_len,
                        uint8_t* response,
                        size_t response_cap,
                        jw_command_handler_fn handler,
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
