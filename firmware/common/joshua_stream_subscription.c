#include "joshua_stream_subscription.h"

#include <string.h>

jw_result_t jw_stream_subscription_init(jw_stream_subscription_t* subscription,
                                        uint8_t function_id,
                                        uint32_t min_period_us,
                                        uint32_t max_period_us) {
  if (subscription == NULL || function_id == JW_CHANNEL_NONE || min_period_us == 0 ||
      max_period_us < min_period_us)
    return JW_RESULT_ERROR;
  const jw_stream_subscription_t initial = {
      function_id, min_period_us, max_period_us, 0, {0, 0, 0}};
  *subscription = initial;
  return JW_RESULT_OK;
}
void jw_stream_subscription_reset(jw_stream_subscription_t* subscription) {
  if (subscription == NULL) return;
  subscription->session_id = 0;
  memset(&subscription->state, 0, sizeof(subscription->state));
}

static int reject(uint8_t* reply, size_t capacity, jw_status_t status) {
  return jw_encode_reply_payload(reply, capacity, status, JW_EXECUTION_REJECTED, NULL, 0);
}

int jw_stream_subscription_handle(jw_stream_subscription_t* subscription,
                                  const jw_command_t* command,
                                  uint8_t* reply,
                                  size_t capacity) {
  // Preflight the largest successful reply before making any state change.
  if (subscription == NULL || command == NULL || reply == NULL ||
      capacity < JW_REPLY_HEADER_LEN + JW_STREAM_STATE_LEN || subscription->min_period_us == 0 ||
      subscription->max_period_us < subscription->min_period_us || command->session_id == 0 ||
      command->message_id == 0)
    return -1;
  if (command->cmd != JW_CMD_PROFILE) return reject(reply, capacity, JW_STATUS_UNSUPPORTED);
  if (command->channel != subscription->function_id)
    return reject(reply, capacity, JW_STATUS_INVALID_CHANNEL);
  if (subscription->session_id != 0 && subscription->session_id != command->session_id)
    return reject(reply, capacity, JW_STATUS_UNAVAILABLE);
  jw_profile_request_t profile;
  if (jw_decode_profile_request(command->payload, command->payload_len, &profile) != JW_RESULT_OK)
    return reject(reply, capacity, JW_STATUS_INVALID_PAYLOAD);
  if (profile.profile_id != JW_PROFILE_SENSOR || profile.major != JW_PROFILE_VERSION_MAJOR ||
      profile.minor != JW_SENSOR_STREAM_MINOR)
    return reject(reply, capacity, JW_STATUS_PROFILE_MISMATCH);
  jw_stream_state_t next = subscription->state;
  uint32_t value;
  switch (profile.operation) {
    case JW_SENSOR_SUBSCRIBE:
      if (jw_decode_stream_period(profile.data, profile.data_len, &value) != JW_RESULT_OK ||
          value < subscription->min_period_us || value > subscription->max_period_us)
        return reject(reply, capacity, JW_STATUS_INVALID_PAYLOAD);
      if (next.active) return reject(reply, capacity, JW_STATUS_BUSY);
      next.active = 1;
      next.subscription_id = command->message_id;
      next.period_us = value;
      break;
    case JW_SENSOR_UNSUBSCRIBE:
      if (jw_decode_stream_token(profile.data, profile.data_len, &value) != JW_RESULT_OK)
        return reject(reply, capacity, JW_STATUS_INVALID_PAYLOAD);
      if (next.subscription_id != value) return reject(reply, capacity, JW_STATUS_UNAVAILABLE);
      next.active = 0;
      break;
    case JW_SENSOR_GET_SUBSCRIPTION:
      if (profile.data_len != 0) return reject(reply, capacity, JW_STATUS_INVALID_PAYLOAD);
      break;
    default:
      return reject(reply, capacity, JW_STATUS_UNSUPPORTED);
  }
  uint8_t state[JW_STREAM_STATE_LEN];
  if (jw_encode_stream_state(state, sizeof(state), &next) < 0) return -1;
  const int length = jw_encode_reply_payload(
      reply, capacity, JW_STATUS_OK, JW_EXECUTION_COMPLETE, state, sizeof(state));
  if (length > 0 && profile.operation != JW_SENSOR_GET_SUBSCRIPTION) {
    subscription->session_id = command->session_id;
    subscription->state = next;
  }
  return length;
}
