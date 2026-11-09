// Portable scalar-f32 acquisition. All hooks are bounded, nonblocking register /
// state operations. Conversion/bus waiting never occurs in command dispatch.
#pragma once
#include "joshua_device_runtime.h"
#include "joshua_stream_subscription.h"
#include "joshua_wire_resources.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void* context;
  int (*start)(void*);         // Start conversion; 1 accepted, 0 failed immediately.
  int (*poll)(void*, float*);  // 1 complete, 0 pending, -1 failed; never wait.
  void (*cancel)(void*);
} jw_scalar_acquisition_t;
typedef struct {
  jw_runtime_function_t* function;
  jw_stream_subscription_t subscription;
  jw_sensor_schema_t schema;
  uint8_t schema_bytes[JW_SENSOR_SCHEMA_LEN];
  jw_sensor_sample_t latest;
  jw_scalar_acquisition_t acquisition;
  jw_resource_claim_t* resources;
  size_t resource_capacity;
  uint16_t pin;
  uint32_t boot_id, acquisition_timeout_us;
  uint64_t now_us, started_us, next_acquisition_us, last_submission_us;
  uint32_t sequence, dropped_acquisitions;
  int pending, in_flight, blocked, ever_acquired;
} jw_scalar_sensor_t;

int jw_scalar_sensor_init(jw_scalar_sensor_t*,
                          jw_runtime_function_t*,
                          uint8_t function_id,
                          uint16_t device_id,
                          uint16_t native_pin,
                          uint32_t boot_id,
                          uint32_t min_period_us,
                          uint32_t max_period_us,
                          uint32_t acquisition_timeout_us,
                          uint32_t max_age_us,
                          uint16_t unit,
                          jw_scalar_acquisition_t,
                          jw_resource_claim_t*,
                          size_t resource_capacity);
void jw_scalar_sensor_service(jw_scalar_sensor_t*, uint64_t now_us);
// 0 reset (preserves a latch), 1 stop/fault, 2 deliberate recover. Never enables.
void jw_scalar_sensor_safety(jw_scalar_sensor_t*, int reason);
int jw_scalar_sensor_prepare(jw_scalar_sensor_t*, uint8_t*, size_t, uint32_t* sequence);
void jw_scalar_sensor_submitted(jw_scalar_sensor_t*, uint32_t sequence);
#ifdef __cplusplus
}
#endif
