// Nonblocking STEP/DIR/ENABLE backend shared by Arduino-based MCU images.
#pragma once

#include "joshua_wire_profiles.h"
#include "stepdir_channel.h"

#define JW_STEPDIR_DEFAULT_PULSE_WIDTH_US 20u

#ifdef __cplusplus
extern "C" {
#endif
// Init does not touch GPIO: pin assignments come from PROFILE/CONFIGURE.
void StepDirInit(ChannelState* channel);
// Call only after complete payload/pin/resource preflight with outputs disabled.
// Applies inactive pin levels, clears the old target, and leaves outputs disabled.
void StepDirConfigure(ChannelState* channel, const jw_configure_step_dir_t* config);
void StepDirEnable(ChannelState* channel);
// Synchronously deasserts ENABLE and STEP without waiting for a pulse timer.
void StepDirDisable(ChannelState* channel);
void StepDirSetTarget(ChannelState* channel, jw_mode_t mode, float value);
// Services at most one edge per loop, never sleeps. Position moves use the
// configured rate; velocity is bounded by it. There is no acceleration ramp.
void StepDirService(ChannelState* channel);
#ifdef __cplusplus
}
#endif
