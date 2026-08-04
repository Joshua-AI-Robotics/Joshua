// STEP/DIR/ENA pulse generation for a channel driving a TB6600 or any other
// STEP/DIR stepper drive — this file toggles two pins, it never names the
// chip (docs/BOARD_LAYER_RFC.md §5.2, §7.4).
#pragma once

#include "channel_table.h"
#include "joshua_wire_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

// State-only — no pin numbers known yet, so no GPIO calls here. Pins are
// host-configured (docs/BOARD_LAYER_RFC.md §7.5, revised); see
// StepDirConfigure.
void StepDirInit(ChannelState* channel);

// Applies the pin assignment and tunables from a CONFIGURE_CHANNEL command:
// pinMode()s step_pin/dir_pin/enable_pin for the first time (idempotent if
// called again), then disables the channel as a safe default until an
// explicit Enable() arrives. Enable()/SetTarget() are no-ops before this
// has run at least once (see pins_configured in channel_table.h).
void StepDirConfigure(ChannelState* channel, const jw1_configure_step_dir_t* config);
void StepDirEnable(ChannelState* channel);
void StepDirDisable(ChannelState* channel);
void StepDirSetTarget(ChannelState* channel, jw1_mode_t mode, float value);

// Call every loop() iteration for every channel: issues at most one pulse
// per call, throttled to config.max_pulse_rate_hz. No acceleration ramp in
// this first cut — every step is issued at the configured max rate, which
// is enough to prove the wire protocol and channel-table contract end to
// end (docs/BOARD_LAYER_RFC.md §10 Phase 5); ramping is a follow-up once
// there's real hardware to tune it against.
void StepDirService(ChannelState* channel);

#ifdef __cplusplus
}
#endif
