// Per-image channel storage shared by the Arduino STEP/DIR backend and runtime.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "joshua_actuator_handler.h"
#include "joshua_wire_resources.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  jw_configure_step_dir_t config;
  int32_t position_steps;  // Counted pulses, not encoder feedback.
  uint32_t last_step_us;
  bool step_high;
} StepDirState;

typedef struct {
  bool configured;
  bool enabled;
  jw_mode_t target_mode;
  float target_value;
  StepDirState step_dir;
  jw_feedback_t feedback;
  jw_actuator_handler_t actuator;
} ChannelState;

// The image declares equally sized channel/function arrays and three exclusive
// GPIO resource slots per channel. There are no default GPIO assignments.
extern ChannelState g_channels[];
extern jw_runtime_function_t g_functions[];
extern jw_resource_claim_t g_stepdir_resources[];
extern const uint8_t g_num_channels;
#ifdef __cplusplus
}
#endif
