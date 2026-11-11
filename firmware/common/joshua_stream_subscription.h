// Portable, allocation-free sensor subscription lifecycle. Callers serialize
// access and reset it with the MCU session. No acquisition, queues or device I/O.
#pragma once

#include "joshua_wire_sensor_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t function_id;
  uint32_t min_period_us;
  uint32_t max_period_us;
  uint32_t session_id;
  jw_stream_state_t state;
} jw_stream_subscription_t;

// Period limits come from the backend's qualified workload, not global defaults.
jw_result_t jw_stream_subscription_init(jw_stream_subscription_t* subscription,
                                        uint8_t function_id,
                                        uint32_t min_period_us,
                                        uint32_t max_period_us);
void jw_stream_subscription_reset(jw_stream_subscription_t* subscription);
// Handles SUBSCRIBE/UNSUBSCRIBE/GET_SUBSCRIPTION only; the sensor backend still
// implements backward-compatible READ. Caller gates advertised capabilities.
// Returns common status/execution plus state, or -1 before mutation if the
// arguments/output capacity are invalid. Use behind a session/endpoint enforcing
// increasing request IDs and exact retries; this helper owns no replay cache.
int jw_stream_subscription_handle(jw_stream_subscription_t* subscription,
                                  const jw_command_t* command,
                                  uint8_t* reply,
                                  size_t capacity);

#ifdef __cplusplus
}
#endif
