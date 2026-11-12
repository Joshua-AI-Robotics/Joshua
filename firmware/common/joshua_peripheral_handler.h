#pragma once
#include "joshua_device_runtime.h"
#include "joshua_wire_peripheral.h"
#include "joshua_wire_resources.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  void* context;
  // Pure route/board capability validation, before claims or hardware changes.
  jw_status_t (*validate)(void*, const jw_peripheral_config_t*);
  jw_status_t (*configure)(void*, unsigned, const jw_peripheral_config_t*);
  jw_status_t (*execute)(void*, unsigned, uint8_t, const uint8_t*, size_t, uint8_t*, size_t*);
  jw_status_t (*release)(void*, unsigned);
  int (*bus_allowed)(void*);
} jw_peripheral_ops_t;
typedef struct jw_peripheral_group jw_peripheral_group_t;
typedef struct {
  jw_peripheral_group_t* group;
  jw_runtime_function_t* function;
  jw_peripheral_config_t config;
  unsigned index;
  int configured, enabled;
} jw_peripheral_slot_t;
struct jw_peripheral_group {
  jw_device_runtime_t* runtime;
  jw_resource_claim_t* resources;
  size_t capacity;
  jw_peripheral_ops_t ops;
  jw_peripheral_slot_t slots[JW_PERIPHERAL_SLOTS];
};
void jw_peripheral_group_init(jw_peripheral_group_t*,
                              jw_device_runtime_t*,
                              jw_runtime_function_t* functions,
                              jw_resource_claim_t*,
                              size_t,
                              jw_peripheral_ops_t);
// Called by the runtime's shared safety callback after stopping actuators.
// Releases every configured backend; does not clear safety latches.
void jw_peripheral_group_reset(jw_peripheral_group_t*);
int jw_peripheral_handle(void*, const jw_command_t*, const jw_profile_request_t*, uint8_t*, size_t);
#ifdef __cplusplus
}
#endif
