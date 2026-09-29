// Implements firmware-side session state above the stateless v2 codec:
// reset gating, monotonically increasing request IDs and cached retry replies.
// Board callbacks perform reset/command actions in the firmware dispatch loop.
#include "joshua_wire_v2_firmware_session.h"

#include <string.h>

void jw2_firmware_session_init(jw2_firmware_session_t* session) {
  if (session != NULL) memset(session, 0, sizeof(*session));
}

int jw2_firmware_session_process(jw2_firmware_session_t* session,
                                 const uint8_t* request,
                                 size_t request_len,
                                 uint8_t* response,
                                 size_t response_cap,
                                 jw2_command_handler_t handler,
                                 jw2_reset_handler_t reset,
                                 void* context) {
  if (session == NULL || response == NULL || response_cap < JW2_MAX_FRAME_LEN || handler == NULL ||
      reset == NULL) {
    return -1;
  }
  jw2_frame_t frame;
  if (jw2_decode_frame(request, request_len, &frame) != 0) return 0;
  const int is_reset = frame.cmd == JW_CMD_RESET_SESSION;
  if (is_reset && (frame.channel != JW_CHANNEL_NONE || frame.payload_len != 0)) return 0;

  if (frame.session_id != session->session_id) {
    if (!is_reset) return 0;
    // Disable outputs before publishing the new session or resetting replay
    // state. The caller serializes this operation with channel service.
    reset(context);
    jw2_firmware_session_init(session);
    session->session_id = frame.session_id;
  } else {
    if (frame.message_id == session->last_message_id && request_len == session->last_request_len &&
        memcmp(request, session->last_request, request_len) == 0) {
      memcpy(response, session->response, session->response_len);
      return session->response_len;
    }
    // Reusing a session to reset would allow old IDs to execute again.
    if (is_reset || frame.message_id <= session->last_message_id) return 0;
  }

  session->last_message_id = frame.message_id;
  memcpy(session->last_request, request, request_len);
  session->last_request_len = (uint8_t)request_len;
  session->response_len = 0;
  uint8_t payload[JW2_MAX_PAYLOAD_LEN];
  int payload_len;
  if (is_reset) {
    payload[0] = 0;
    payload_len = 1;
  } else {
    payload_len = handler(context, &frame, payload, sizeof(payload));
  }
  if (payload_len < 0 || payload_len > JW2_MAX_PAYLOAD_LEN) return -1;
  const int len = jw2_encode_response(
      session->response, sizeof(session->response), &frame, payload, (uint8_t)payload_len);
  if (len < 0) return -1;
  session->response_len = (uint8_t)len;
  memcpy(response, session->response, session->response_len);
  return len;
}
