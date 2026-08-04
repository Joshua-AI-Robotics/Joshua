// The channel table is the pinout contract between the person wiring the
// robot and the person writing config (docs/BOARD_LAYER_RFC.md §7.5): pin
// numbers appear in exactly this one file. Rewiring a motor to a different
// pin is a channel_table.c edit + reflash with the host untouched;
// retuning a motor (max pulse rate, direction, idle position) is a pbtxt
// edit with the firmware untouched.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "joshua_wire_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t step_pin;
  uint8_t dir_pin;
  uint8_t enable_pin;
} ChannelPins;

typedef struct {
  ChannelPins pins;
  jw1_configure_step_dir_t config;  // Pushed by CONFIGURE_CHANNEL at host Init().
  bool enabled;
  jw1_mode_t target_mode;
  float target_value;          // Steps (kPosition) or steps/sec (kVelocity).
  long position_steps;         // Open-loop: counted pulses, not measured.
  unsigned long last_step_us;  // micros() timestamp of the last pulse, for
                               // max_pulse_rate_hz throttling.
} ChannelState;

// One entry per wiring variant (docs/BOARD_LAYER_RFC.md §7.4); defined in
// channel_table.c, this variant's pinout contract.
extern ChannelState g_channels[];
extern const uint8_t g_num_channels;

#ifdef __cplusplus
}
#endif
