#include "joshua_scalar_sensor.h"

#include <math.h>
#include <string.h>

static int rejected(uint8_t* out, size_t cap, jw_status_t status) {
  return jw_encode_reply_payload(out, cap, status, JW_EXECUTION_REJECTED, NULL, 0);
}
static void release(jw_scalar_sensor_t* s) {
  jw_resource_replace(
      s->resources, s->resource_capacity, s->function->descriptor.function_id, NULL, 0, NULL);
}
static void acquired(jw_scalar_sensor_t* s, int valid, float value) {
  if (s->pending && s->dropped_acquisitions != UINT32_MAX) ++s->dropped_acquisitions;
  ++s->sequence;
  s->latest.value = valid && isfinite(value) ? value : NAN;
  s->latest.metadata =
      (jw_measurement_metadata_t){valid && isfinite(value) ? JW_SAMPLE_VALID : JW_SAMPLE_INVALID,
                                  JW_CLOCK_BOOT_MONOTONIC_US,
                                  s->boot_id,
                                  s->started_us,
                                  s->sequence};
  s->ever_acquired = 1;
  s->pending = 1;
  s->in_flight = 0;
}
static int command(void* context,
                   const jw_command_t* cmd,
                   const jw_profile_request_t* p,
                   uint8_t* out,
                   size_t cap) {
  jw_scalar_sensor_t* s = (jw_scalar_sensor_t*)context;
  if (p->operation == JW_SENSOR_READ) {
    if (p->data_len) return rejected(out, cap, JW_STATUS_INVALID_PAYLOAD);
    if (!s->subscription.state.active || s->blocked)
      return rejected(out, cap, JW_STATUS_UNAVAILABLE);
    if (!s->ever_acquired) return rejected(out, cap, JW_STATUS_NOT_READY);
    jw_sensor_sample_t sample = s->latest;
    if (s->now_us - sample.metadata.timestamp_us > s->schema.max_age_us &&
        sample.metadata.validity == JW_SAMPLE_VALID)
      sample.metadata.validity = JW_SAMPLE_STALE;
    uint8_t bytes[JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN];
    const int length = jw_encode_sensor_sample_payload(
        bytes, sizeof(bytes), JW_SENSOR_INTERFACE_SCALAR_F32, &sample);
    return length < 0 ? -1
                      : jw_encode_reply_payload(
                            out, cap, JW_STATUS_OK, JW_EXECUTION_COMPLETE, bytes, length);
  }
  if (s->blocked && p->operation != JW_SENSOR_GET_SUBSCRIPTION)
    return rejected(out, cap, JW_STATUS_UNAVAILABLE);
  jw_stream_subscription_t candidate = s->subscription;
  const int length = jw_stream_subscription_handle(&candidate, cmd, out, cap);
  jw_reply_t reply;
  if (length <= 0 || jw_decode_reply_payload(out, length, &reply) != JW_RESULT_OK ||
      reply.status != JW_STATUS_OK)
    return length;
  if (p->operation == JW_SENSOR_SUBSCRIBE) {
    const jw_resource_claim_t claims[] = {
        {s->function->descriptor.function_id, JW_RESOURCE_PIN, s->pin, 0, 0},
        {s->function->descriptor.function_id, JW_RESOURCE_ADC, 0, 0, 0}};
    jw_resource_conflict_t conflict;
    const jw_status_t status = jw_resource_replace(s->resources,
                                                   s->resource_capacity,
                                                   s->function->descriptor.function_id,
                                                   claims,
                                                   2,
                                                   &conflict);
    if (status == JW_STATUS_RESOURCE_CONFLICT) {
      const uint8_t detail[] = {
          conflict.owner, conflict.kind, (uint8_t)conflict.id, (uint8_t)(conflict.id >> 8)};
      return jw_encode_reply_payload(
          out, cap, status, JW_EXECUTION_REJECTED, detail, sizeof(detail));
    }
    if (status != JW_STATUS_OK) return rejected(out, cap, status);
    s->pending = s->in_flight = s->ever_acquired = 0;
    s->dropped_acquisitions = 0;
    s->next_acquisition_us = s->now_us;
    s->last_submission_us = 0;
    s->function->state.state = JW_STATE_ACTIVE;
  } else if (p->operation == JW_SENSOR_UNSUBSCRIBE) {
    s->acquisition.cancel(s->acquisition.context);
    s->pending = s->in_flight = 0;
    release(s);
    s->function->state.state = JW_STATE_DISABLED;
  }
  s->subscription = candidate;
  if (p->operation != JW_SENSOR_GET_SUBSCRIPTION) {
    ++s->function->state.epoch;
    s->function->state.reason = JW_REASON_NONE;
    s->function->state.recovery = 0;
    s->function->state.stop_phase = JW_STOP_NONE;
    s->function->state.verification = JW_SAFE_UNSUPPORTED;
    s->function->state.action_session_id = cmd->session_id;
    s->function->state.action_message_id = cmd->message_id;
    s->function->state.action_status = JW_STATUS_OK;
    s->function->state.action_execution = JW_EXECUTION_COMPLETE;
  }
  return length;
}
int jw_scalar_sensor_init(jw_scalar_sensor_t* s,
                          jw_runtime_function_t* function,
                          uint8_t id,
                          uint16_t device_id,
                          uint16_t pin,
                          uint32_t boot_id,
                          uint32_t min_period,
                          uint32_t max_period,
                          uint32_t timeout,
                          uint32_t max_age,
                          uint16_t unit,
                          jw_scalar_acquisition_t acquisition,
                          jw_resource_claim_t* resources,
                          size_t capacity) {
  if (!s || !function || !boot_id || !timeout || !max_age || !acquisition.start ||
      !acquisition.poll || !acquisition.cancel || !resources || capacity < 2)
    return 0;
  memset(s, 0, sizeof(*s));
  memset(function, 0, sizeof(*function));
  if (jw_stream_subscription_init(&s->subscription, id, min_period, max_period) != JW_RESULT_OK)
    return 0;
  s->function = function;
  s->pin = pin;
  s->boot_id = boot_id;
  s->acquisition_timeout_us = timeout;
  s->acquisition = acquisition;
  s->resources = resources;
  s->resource_capacity = capacity;
  s->schema = (jw_sensor_schema_t){
      JW_SENSOR_INTERFACE_SCALAR_F32, unit, JW_CLOCK_BOOT_MONOTONIC_US, max_age};
  if (jw_encode_sensor_schema(s->schema_bytes, sizeof(s->schema_bytes), &s->schema) < 0) return 0;
  function->descriptor = (jw_function_descriptor_t){
      id, device_id, JW_PROFILE_SENSOR, 1, JW_SENSOR_STREAM_MINOR, JW_SENSOR_STREAM_OPERATIONS, 0};
  function->schema = s->schema_bytes;
  function->schema_len = sizeof(s->schema_bytes);
  function->handler = command;
  function->context = s;
  return 1;
}
void jw_scalar_sensor_service(jw_scalar_sensor_t* s, uint64_t now) {
  s->now_us = now;
  if (s->blocked || !s->subscription.state.active) return;
  if (s->in_flight) {
    float value = NAN;
    if (now - s->started_us >= s->acquisition_timeout_us) {
      s->acquisition.cancel(s->acquisition.context);
      acquired(s, 0, NAN);
    } else {
      const int result = s->acquisition.poll(s->acquisition.context, &value);
      if (result != 0) acquired(s, result > 0, value);
    }
  }
  if (!s->in_flight && now >= s->next_acquisition_us) {
    s->started_us = now;
    s->next_acquisition_us = now + s->subscription.state.period_us;
    s->in_flight = 1;
    if (!s->acquisition.start(s->acquisition.context)) acquired(s, 0, NAN);
  }
}
void jw_scalar_sensor_safety(jw_scalar_sensor_t* s, int reason) {
  s->acquisition.cancel(s->acquisition.context);
  s->pending = s->in_flight = s->ever_acquired = 0;
  if (reason == 1)
    s->blocked = 1;
  else if (reason == 2)
    s->blocked = 0;
  jw_stream_subscription_reset(&s->subscription);
  release(s);
}
int jw_scalar_sensor_prepare(jw_scalar_sensor_t* s,
                             uint8_t* out,
                             size_t capacity,
                             uint32_t* sequence) {
  if (!s->subscription.state.active || s->blocked || !s->pending || !sequence ||
      (s->last_submission_us &&
       s->now_us - s->last_submission_us < s->subscription.state.period_us))
    return 0;
  jw_stream_sample_t value = {s->subscription.session_id,
                              s->subscription.state.subscription_id,
                              s->function->descriptor.function_id,
                              s->dropped_acquisitions,
                              s->latest};
  if (s->now_us - value.sample.metadata.timestamp_us > s->schema.max_age_us &&
      value.sample.metadata.validity == JW_SAMPLE_VALID)
    value.sample.metadata.validity = JW_SAMPLE_STALE;
  *sequence = value.sample.metadata.sequence;
  return jw_encode_stream_sample(out, capacity, &value);
}
void jw_scalar_sensor_submitted(jw_scalar_sensor_t* s, uint32_t sequence) {
  if (s->latest.metadata.sequence == sequence) s->pending = 0;
  s->last_submission_us = s->now_us;
}
