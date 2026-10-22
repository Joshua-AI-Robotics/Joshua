// Implements firmware-side session state above the stateless JW codec:
// reset gating, monotonically increasing request IDs and cached retry replies.
// Board callbacks perform reset/command actions in the firmware dispatch loop.
#include "joshua_wire_firmware_session.h"

#include <string.h>

void jw_firmware_session_init(jw_firmware_session_t* session) {
  if (session != NULL) memset(session, 0, sizeof(*session));
}

int jw_firmware_session_process(jw_firmware_session_t* session,
                                const uint8_t* request,
                                size_t request_len,
                                uint8_t* response,
                                size_t response_cap,
                                jw_command_handler_t handler,
                                jw_reset_handler_t reset,
                                void* context) {
  if (session == NULL || response == NULL || response_cap < JW_MAX_FRAME_LEN || handler == NULL ||
      reset == NULL) {
    return -1;
  }
  jw_frame_t frame;
  if (jw_decode_frame(request, request_len, &frame) != JW_RESULT_OK) return 0;
  const int is_reset = frame.cmd == JW_CMD_RESET_SESSION;
  if (is_reset && (frame.channel != JW_CHANNEL_NONE || frame.payload_len != 0)) return 0;

  if (frame.session_id != session->session_id) {
    if (!is_reset) return 0;
    // Disable outputs before publishing the new session or resetting replay
    // state. The caller serializes this operation with channel service.
    reset(context);
    jw_firmware_session_init(session);
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
  uint8_t payload[JW_MAX_PAYLOAD_LEN];
  int payload_len;
  if (is_reset) {
    payload_len = jw_encode_reply_payload(
        payload, sizeof(payload), JW_STATUS_OK, JW_EXECUTION_COMPLETE, NULL, 0);
  } else {
    payload_len = handler(context, &frame, payload, sizeof(payload));
  }
  jw_reply_t reply;
  // A handler failure cannot prove that no side effects occurred. Cache the
  // unknown outcome too, so a lost/malformed reply can never execute twice.
  if (payload_len < 0 || payload_len > JW_MAX_PAYLOAD_LEN ||
      jw_decode_reply_payload(payload, (size_t)payload_len, &reply) != JW_RESULT_OK) {
    payload_len = jw_encode_reply_payload(
        payload, sizeof(payload), JW_STATUS_OUTCOME_UNKNOWN, JW_EXECUTION_UNKNOWN, NULL, 0);
  }
  const int len = jw_encode_response(
      session->response, sizeof(session->response), &frame, payload, (uint8_t)payload_len);
  if (len < 0) return -1;
  session->response_len = (uint8_t)len;
  memcpy(response, session->response, session->response_len);
  return len;
}
