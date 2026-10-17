// Native declarations for capturing the production watchdog task registration.
#pragma once
#include <stdint.h>

#define TaskP_PRIORITY_HIGHEST 31u
#define SystemP_SUCCESS 0

typedef struct {
  uintptr_t unused;
} TaskP_Object;
typedef struct {
  const char* name;
  void* stack;
  uint32_t stackSize;
  uint32_t priority;
  void (*taskMain)(void*);
  void* args;
} TaskP_Params;

#ifdef __cplusplus
extern "C" {
#endif
void TaskP_Params_init(TaskP_Params* params);
int32_t TaskP_construct(TaskP_Object* task, TaskP_Params* params);
#ifdef __cplusplus
}
#endif
