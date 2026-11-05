// Shared actuator-profile execution adapter. No GPIO, vendor SDK or transport I/O.
#pragma once

#include <stdbool.h>

#include "joshua_device_runtime.h"
#include "joshua_wire_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool configured;
  bool enabled;
  bool estopped;
} jw_actuator_state_t;

typedef struct {
  jw_status_t status;
  jw_execution_t execution;
  jw_resource_conflict_t conflict;  // Valid only for RESOURCE_CONFLICT.
} jw_actuator_result_t;

// Convenience for synchronous backends only: OK/ERROR mean COMPLETE; admission
// errors mean REJECTED; OUTCOME_UNKNOWN stays UNKNOWN. Async callbacks must return
// OK/ACCEPTED explicitly and publish completion in their function's state later.
jw_actuator_result_t jw_actuator_sync_result(jw_status_t status);

typedef struct {
  jw_actuator_state_t (*state)(void*);
  jw_actuator_result_t (*configure_step_dir)(void*, const jw_configure_step_dir_t*);
  jw_actuator_result_t (*set_target)(void*, const jw_set_target_t*);
  jw_actuator_result_t (*enable)(void*);
  jw_actuator_result_t (*disable)(void*);
  // Cached acquisition only; return OK/COMPLETE with a sample, or a rejection.
  jw_actuator_result_t (*feedback)(void*, jw_feedback_t*);
} jw_actuator_backend_ops_t;

typedef struct {
  jw_runtime_function_t* function;
  jw_actuator_schema_t schema;  // Must match the function's advertised metadata.
  const jw_actuator_backend_ops_t* ops;
  void* context;
} jw_actuator_handler_t;

// Register as jw_runtime_function_t.handler with a borrowed jw_actuator_handler_t
// context. All storage outlives dispatch; one MCU owner serializes callbacks,
// safety actions, state reads and acquisition. Callbacks must be bounded.
//
// The shared handler checks payloads/modes and common configure/enable guards.
// Backends retain device limits, pending-work/interlock checks, resource staging
// and rollback. A rejected callback must have no effects. ERROR/UNKNOWN/ACCEPTED
// callbacks publish authoritative device state before returning; the handler only
// attaches action IDs/execution and never infers successful hardware completion.
// OK/COMPLETE mutations use a fresh backend snapshot for normal state reporting.
// Reset/ESTOP/recovery remain board-wide runtime safety callbacks, outside this
// profile. This handler neither owns a session nor clears safety latches.
int jw_actuator_handle(void* context,
                       const jw_command_t* command,
                       const jw_profile_request_t* request,
                       uint8_t* response,
                       size_t capacity);

#ifdef __cplusplus
}
#endif
