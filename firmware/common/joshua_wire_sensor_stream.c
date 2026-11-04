#include "joshua_wire_sensor_stream.h"

#include <string.h>

#if JW_STREAM_SAMPLE_PAYLOAD_LEN > JW_MAX_PAYLOAD_LEN
#error "Stream samples must fit in one bounded JoshuaWire frame"
#endif

static void put_u32(uint8_t* out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (8 * i));
}
static uint32_t get_u32(const uint8_t* in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}
static int valid_state(const jw_stream_state_t* state) {
  return state != NULL && state->active <= 1 &&
         ((state->subscription_id != 0 && state->period_us != 0) ||
          (state->active == 0 && state->subscription_id == 0 && state->period_us == 0));
}

int jw_sensor_supports_stream(const jw_function_descriptor_t* function) {
  return function != NULL && function->function_id != JW_CHANNEL_NONE &&
         function->profile_id == JW_PROFILE_SENSOR &&
         function->profile_major == JW_PROFILE_VERSION_MAJOR &&
         function->profile_minor >= JW_SENSOR_STREAM_MINOR &&
         (function->operations & JW_SENSOR_STREAM_OPERATIONS) == JW_SENSOR_STREAM_OPERATIONS;
}

int jw_encode_stream_period(uint8_t* out, size_t cap, uint32_t period_us) {
  if (out == NULL || cap < JW_STREAM_PERIOD_LEN || period_us == 0) return -1;
  put_u32(out, period_us);
  return JW_STREAM_PERIOD_LEN;
}
jw_result_t jw_decode_stream_period(const uint8_t* data, size_t len, uint32_t* out) {
  if (data == NULL || out == NULL || len != JW_STREAM_PERIOD_LEN || get_u32(data) == 0)
    return JW_RESULT_ERROR;
  *out = get_u32(data);
  return JW_RESULT_OK;
}
int jw_encode_stream_token(uint8_t* out, size_t cap, uint32_t subscription_id) {
  return jw_encode_stream_period(out, cap, subscription_id);
}
jw_result_t jw_decode_stream_token(const uint8_t* data, size_t len, uint32_t* out) {
  return jw_decode_stream_period(data, len, out);
}

int jw_encode_stream_state(uint8_t* out, size_t cap, const jw_stream_state_t* state) {
  if (out == NULL || cap < JW_STREAM_STATE_LEN || !valid_state(state)) return -1;
  out[0] = state->active;
  put_u32(out + 1, state->subscription_id);
  put_u32(out + 5, state->period_us);
  return JW_STREAM_STATE_LEN;
}
jw_result_t jw_decode_stream_state(const uint8_t* data, size_t len, jw_stream_state_t* out) {
  if (data == NULL || out == NULL || len != JW_STREAM_STATE_LEN) return JW_RESULT_ERROR;
  const jw_stream_state_t state = {data[0], get_u32(data + 1), get_u32(data + 5)};
  if (!valid_state(&state)) return JW_RESULT_ERROR;
  *out = state;
  return JW_RESULT_OK;
}

int jw_encode_stream_sample(uint8_t* out, size_t cap, const jw_stream_sample_t* sample) {
  if (out == NULL || sample == NULL || cap < JW_STREAM_SAMPLE_FRAME_LEN ||
      sample->function_id == JW_CHANNEL_NONE || sample->session_id == 0 ||
      sample->subscription_id == 0)
    return -1;
  uint8_t payload[JW_STREAM_SAMPLE_PAYLOAD_LEN];
  const int length = jw_encode_sensor_sample_payload(payload + JW_STREAM_SAMPLE_PREFIX_LEN,
                                                     JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN,
                                                     JW_SENSOR_INTERFACE_SCALAR_F32,
                                                     &sample->sample);
  if (length < 0) return -1;
  payload[0] = JW_PROFILE_SENSOR;
  payload[1] = 0;
  payload[2] = JW_PROFILE_VERSION_MAJOR;
  payload[3] = JW_SENSOR_STREAM_MINOR;
  payload[4] = JW_SENSOR_INTERFACE_SCALAR_F32;
  put_u32(payload + 5, sample->dropped_acquisitions);
  return jw_encode_frame(out,
                         cap,
                         sample->session_id,
                         sample->subscription_id,
                         JW_CMD_STREAM_SAMPLE,
                         sample->function_id,
                         payload,
                         sizeof(payload));
}
jw_result_t jw_decode_stream_sample(const uint8_t* data, size_t len, jw_stream_sample_t* out) {
  if (out == NULL) return JW_RESULT_ERROR;
  jw_frame_t frame;
  if (jw_decode_frame(data, len, &frame) != JW_RESULT_OK || frame.cmd != JW_CMD_STREAM_SAMPLE ||
      frame.channel == JW_CHANNEL_NONE || frame.payload_len != JW_STREAM_SAMPLE_PAYLOAD_LEN)
    return JW_RESULT_ERROR;
  const uint8_t* payload = frame.payload;
  if (payload[0] != JW_PROFILE_SENSOR || payload[1] != 0 ||
      payload[2] != JW_PROFILE_VERSION_MAJOR || payload[3] != JW_SENSOR_STREAM_MINOR ||
      payload[4] != JW_SENSOR_INTERFACE_SCALAR_F32)
    return JW_RESULT_ERROR;
  jw_stream_sample_t sample;
  sample.session_id = frame.session_id;
  sample.subscription_id = frame.message_id;
  sample.function_id = frame.channel;
  sample.dropped_acquisitions = get_u32(payload + 5);
  if (jw_decode_sensor_sample_payload(payload + JW_STREAM_SAMPLE_PREFIX_LEN,
                                      JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN,
                                      JW_SENSOR_INTERFACE_SCALAR_F32,
                                      &sample.sample) != JW_RESULT_OK)
    return JW_RESULT_ERROR;
  *out = sample;
  return JW_RESULT_OK;
}
int jw_stream_sample_matches(const jw_stream_sample_t* sample,
                             uint32_t session_id,
                             uint8_t function_id,
                             const jw_stream_state_t* subscription) {
  return sample != NULL && valid_state(subscription) && subscription->active && session_id != 0 &&
         function_id != JW_CHANNEL_NONE && sample->session_id == session_id &&
         sample->function_id == function_id &&
         sample->subscription_id == subscription->subscription_id;
}
