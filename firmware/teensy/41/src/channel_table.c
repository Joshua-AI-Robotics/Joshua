// firmware/teensy/41/src/channel_table.c — one file per wiring variant
// (docs/BOARD_LAYER_RFC.md §7.5). This variant: a single TB6600 stepper
// drive on channel 0.
//
// Wiring:
//   Teensy 4.1 pin 2  -> TB6600 PUL+ (STEP)
//   Teensy 4.1 pin 3  -> TB6600 DIR+ (DIR)
//   Teensy 4.1 pin 4  -> TB6600 ENA+ (ENABLE)
//   Teensy 4.1 GND    -> TB6600 PUL-/DIR-/ENA- (common ground)
// See docs/bringup.md for the full wiring diagram and TB6600 DIP switch
// settings.
//
// To wire a second TB6600 for channel 1, add an entry here (pick unused
// pins) and reflash — no host-side code changes; the host declares
// channel 1 in its config and IDENTIFY reports it automatically.
#include "channel_table.h"

ChannelState g_channels[] = {
    {.pins = {.step_pin = 2, .dir_pin = 3, .enable_pin = 4}},  // channel 0
};

const uint8_t g_num_channels = sizeof(g_channels) / sizeof(g_channels[0]);
