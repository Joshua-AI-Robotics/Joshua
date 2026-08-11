// The channel table declares how many STEP_DIR channel slots this firmware
// image exposes (docs/BOARD_LAYER_RFC.md §7.4) — a compile-time fact,
// reported via IDENTIFY. Pin numbers are NOT declared here: they're
// host-configured, pushed per channel via CONFIGURE_CHANNEL at Init() and
// applied at runtime (see StepDirConfigure in backend_stepdir.cpp) — see
// robot/board/proto/board.proto's StepDirConfig for why pins moved out of
// the firmware image (docs/BOARD_LAYER_RFC.md §7.5, revised). A channel
// rejects SetTarget/Enable until its first CONFIGURE_CHANNEL arrives
// (ChannelState.configured below) — there's no default pin to fall back on.
//
// Identical shape to firmware/teensy/41/src/channel_table.h — nothing here
// is MCU-specific (docs/BOARD_LAYER_RFC.md §7.3 ④).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "joshua_wire_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

// Backend-specific state for a STEP_DIR channel (docs/BOARD_LAYER_RFC.md
// §7.3 ④). position_steps/last_step_us only make sense because STEP_DIR is
// an open-loop, discrete-pulse drive — a different backend type would have
// its own analogous struct here (e.g. a PWM backend's duty cycle and timer
// handle), not these fields.
typedef struct {
  jw1_configure_step_dir_t config;  // Pushed by CONFIGURE_CHANNEL at host Init();
                                    // includes step_pin/dir_pin/enable_pin.
  long position_steps;              // Open-loop: counted pulses, not measured.
  unsigned long last_step_us;       // micros() timestamp of the last pulse, for
                                    // max_pulse_rate_hz throttling.
} StepDirState;

typedef struct {
  // Generic across any drive backend a channel slot could compile against —
  // nothing above this line assumes STEP_DIR.
  bool configured;  // False until the first CONFIGURE_CHANNEL.
  bool enabled;
  jw1_mode_t target_mode;
  float target_value;  // Native unit; meaning is backend-defined (steps /
                       // steps-per-sec for STEP_DIR).

  // This firmware image's one backend today. A second backend type would
  // add its own sibling field here, not replace this one — deliberately
  // not a union yet, since there's no real second backend to design that
  // union's shape against (docs/BOARD_LAYER_RFC.md §7.3 ④).
  StepDirState step_dir;
} ChannelState;

// One entry per channel slot this firmware image exposes; defined in
// channel_table.c.
extern ChannelState g_channels[];
extern const uint8_t g_num_channels;

#ifdef __cplusplus
}
#endif
