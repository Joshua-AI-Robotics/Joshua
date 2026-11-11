// Opt-in sensor-profile 1.1 stream codecs. No I/O, subscription storage or timers.
#pragma once

#include "joshua_wire.h"
#include "joshua_wire_profiles.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JW_SENSOR_STREAM_MINOR 1
#define JW_SENSOR_SUBSCRIBE 1
#define JW_SENSOR_UNSUBSCRIBE 2
#define JW_SENSOR_GET_SUBSCRIPTION 3
#define JW_SENSOR_STREAM_OPERATIONS                                                       \
  ((1u << JW_SENSOR_READ) | (1u << JW_SENSOR_SUBSCRIBE) | (1u << JW_SENSOR_UNSUBSCRIBE) | \
   (1u << JW_SENSOR_GET_SUBSCRIPTION))
#define JW_STREAM_PERIOD_LEN 4
#define JW_STREAM_TOKEN_LEN 4
#define JW_STREAM_STATE_LEN 9
// profile:u16, major:u8, minor:u8, data_type:u8, dropped_acquisitions:u32.
#define JW_STREAM_SAMPLE_PREFIX_LEN 9
#define JW_STREAM_SAMPLE_PAYLOAD_LEN \
  (JW_STREAM_SAMPLE_PREFIX_LEN + JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN)
#define JW_STREAM_SAMPLE_FRAME_LEN (JW_FRAME_OVERHEAD + JW_STREAM_SAMPLE_PAYLOAD_LEN)

typedef struct {
  uint8_t active;
  // SUBSCRIBE request's message ID. Never reused within a session.
  // Retained after cancellation so its lost reply can be reconciled by query.
  uint32_t subscription_id;
  uint32_t period_us;
} jw_stream_state_t;

typedef struct {
  uint32_t session_id;
  uint32_t subscription_id;  // Notification header ID, not a response ID.
  uint8_t function_id;
  // Saturating count of acquisitions omitted before submission to the link.
  // Sequence gaps can additionally expose link/receiver loss.
  uint32_t dropped_acquisitions;
  jw_sensor_sample_t sample;
} jw_stream_sample_t;

// Require the complete operation set, including backward-compatible v1.0 READ.
int jw_sensor_supports_stream(const jw_function_descriptor_t* function);
int jw_encode_stream_period(uint8_t* out, size_t cap, uint32_t period_us);
jw_result_t jw_decode_stream_period(const uint8_t* data, size_t len, uint32_t* out);
int jw_encode_stream_token(uint8_t* out, size_t cap, uint32_t subscription_id);
jw_result_t jw_decode_stream_token(const uint8_t* data, size_t len, uint32_t* out);
// State is profile reply data; the common status/execution prefix is separate.
int jw_encode_stream_state(uint8_t* out, size_t cap, const jw_stream_state_t* state);
jw_result_t jw_decode_stream_state(const uint8_t* data, size_t len, jw_stream_state_t* out);
// Complete bounded notification frames, with CRC. No common reply prefix.
int jw_encode_stream_sample(uint8_t* out, size_t cap, const jw_stream_sample_t* sample);
jw_result_t jw_decode_stream_sample(const uint8_t* data, size_t len, jw_stream_sample_t* out);
// Identity routing only. Consumers must also validate schema/boot/clock/freshness.
int jw_stream_sample_matches(const jw_stream_sample_t* sample,
                             uint32_t session_id,
                             uint8_t function_id,
                             const jw_stream_state_t* subscription);

#ifdef __cplusplus
}
#endif
