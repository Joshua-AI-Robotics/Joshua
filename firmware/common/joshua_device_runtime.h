// Bounded, allocation-free backend catalog and core dispatch. One runtime and
// endpoint belong to one MCU; adapters must serialize entry to both.
#pragma once
#include "joshua_wire.h"
#include "joshua_wire_profiles.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef int (*jw_profile_handler_fn)(
    void*, const jw_command_t*, const jw_profile_request_t*, uint8_t*, size_t);
typedef void (*jw_safety_handler_fn)(void*, int);
typedef struct {
  jw_function_descriptor_t descriptor;
  const uint8_t* schema;
  size_t schema_len;
  jw_device_status_t state;
  jw_profile_handler_fn handler;
  void* context;
} jw_runtime_function_t;
typedef struct {
  jw_identify_response_t identity;
  jw_runtime_function_t* functions;
  jw_device_status_t state;
  jw_safety_handler_fn safety;  // 0=reset, 1=stop, 2=recover; bounded, never enable.
  void* context;
  int estopped;
} jw_device_runtime_t;
void jw_runtime_reset(void* context);
// Latch a board-local fault without inventing a host action token or changing
// session/replay ownership. Recovery follows RECOVER/configure/explicit enable.
void jw_runtime_fault(jw_device_runtime_t* runtime);
int jw_runtime_command(void* context,
                       const jw_command_t* command,
                       uint8_t* response,
                       size_t capacity);
#ifdef __cplusplus
}
#endif
