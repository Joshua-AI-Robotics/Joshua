// The channel table declares how many STEP_DIR channel slots this firmware
// image exposes (docs/BOARD_LAYER_RFC.md §7.4) — a compile-time fact,
// reported via IDENTIFY. Pin numbers are NOT declared here: they're
// host-configured, pushed per channel via CONFIGURE_CHANNEL at Init() and
// applied at runtime (see StepDirConfigure in backend_stepdir.cpp) — see
// robot/board/proto/board.proto's StepDirConfig for why pins moved out of
// the firmware image (docs/BOARD_LAYER_RFC.md §7.5, revised). A channel
// rejects SetTarget/Enable until its first CONFIGURE_CHANNEL arrives
// (pins_configured below) — there's no default pin to fall back on.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "joshua_wire_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  jw1_configure_step_dir_t config;  // Pushed by CONFIGURE_CHANNEL at host Init();
                                    // includes step_pin/dir_pin/enable_pin.
  bool pins_configured;             // False until the first CONFIGURE_CHANNEL.
  bool enabled;
  jw1_mode_t target_mode;
  float target_value;          // Steps (kPosition) or steps/sec (kVelocity).
  long position_steps;         // Open-loop: counted pulses, not measured.
  unsigned long last_step_us;  // micros() timestamp of the last pulse, for
                               // max_pulse_rate_hz throttling.
} ChannelState;

// One entry per channel slot this firmware image exposes; defined in
// channel_table.c.
extern ChannelState g_channels[];
extern const uint8_t g_num_channels;

#ifdef __cplusplus
}
#endif
