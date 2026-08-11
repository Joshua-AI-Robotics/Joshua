// firmware/teensy/41/src/channel_table.c — declares how many STEP_DIR
// channel slots this firmware image exposes (docs/BOARD_LAYER_RFC.md
// §7.4). This variant: one channel slot.
//
// Pin numbers are NOT declared here — they're host-configured via
// StepDirConfig.step_pin/dir_pin/enable_pin in the board's pbtxt, pushed
// to this exact channel at Init() via CONFIGURE_CHANNEL
// (docs/BOARD_LAYER_RFC.md §7.5, revised). See the example preset
// (config/config_preset/example/teensy_stepper_demo.pbtxt) for the pin
// values currently wired on the reference bring-up, and
// ../README.md's Wiring / Pinout section for the full wiring diagram.
//
// To add a second channel slot, add an entry here and reflash — no other
// firmware changes; the host then declares a second `channels { index: 1
// step_dir { step_pin: ... dir_pin: ... enable_pin: ... } }` block in its
// config, on whatever free pins it's actually wired to.
#include "channel_table.h"

ChannelState g_channels[1];

const uint8_t g_num_channels = sizeof(g_channels) / sizeof(g_channels[0]);
