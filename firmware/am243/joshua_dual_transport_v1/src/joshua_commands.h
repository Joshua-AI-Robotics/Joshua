// AM243's transport-neutral, software-only channel and command/reset callbacks.
// UART and EtherCAT artifacts reuse these semantics with separate lifecycle
// wrappers. No GPIO or TI SDK dependency. Simultaneous UART/EtherCAT ownership
// still requires an arbiter; the JW2 EtherCAT artifact does not start UART.
#pragma once

#include <stdbool.h>

#include "joshua_wire_commands.h"

typedef struct {
  bool configured;
  bool enabled;
  bool estopped;
  bool latch_estop;
  uint16_t fault_flags;
  jw_configure_step_dir_t config;
  jw_mode_t target_mode;
  float target_value;
} JoshuaChannel;

#ifdef __cplusplus
extern "C" {
#endif

int JoshuaCommand(void* context, const jw_command_t* frame, uint8_t* response, size_t capacity);
void JoshuaReset(void* context);
#ifdef __cplusplus
}
#endif
