// Bounded resource preflight/ownership helper; firmware backends own hardware apply.
#pragma once
#include "joshua_wire_commands.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  JW_RESOURCE_PIN = 1,
  JW_RESOURCE_TIMER = 2,
  JW_RESOURCE_ADC = 3,
  JW_RESOURCE_BUS = 4
} jw_resource_kind_t;
typedef struct {
  uint8_t owner;  // JW_CHANNEL_NONE means free.
  uint8_t kind;
  uint16_t id;
  uint8_t shared;     // Both claims must be shared with identical settings.
  uint32_t settings;  // Backend-defined compatibility key, e.g. PWM frequency.
} jw_resource_claim_t;
typedef struct {
  uint8_t owner;
  uint8_t kind;
  uint16_t id;
} jw_resource_conflict_t;
void jw_resource_table_init(jw_resource_claim_t* table, size_t capacity);
// Atomically replace ALL claims of one function, including release (count=0).
// Validate candidates/capacity/conflicts first. Failure changes no ownership.
// Caller serializes board configuration, checks disabled state, and stages or
// rolls back hardware changes; table commit alone is not hardware atomicity.
// Candidates must not overlap table. Conflict details valid only on CONFLICT.
jw_status_t jw_resource_replace(jw_resource_claim_t* table,
                                size_t capacity,
                                uint8_t owner,
                                const jw_resource_claim_t* candidates,
                                size_t count,
                                jw_resource_conflict_t* conflict);
#ifdef __cplusplus
}
#endif
