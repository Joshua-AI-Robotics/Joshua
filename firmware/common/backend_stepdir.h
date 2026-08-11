// STEP/DIR/ENA pulse generation for a channel driving a TB6600 or any other
// STEP/DIR stepper drive — this file toggles two pins, it never names the
// chip (docs/BOARD_LAYER_RFC.md §5.2, §7.4).
//
// Lives in firmware/common/, not a per-board src/, and is compiled into
// every firmware image that declares a STEP_DIR channel (docs/
// BOARD_LAYER_RFC.md §7.3, revised) — the STEP/DIR signaling convention
// is a physical fact about the driver chip, not an MCU-vendor fact, so
// unlike a PWM or CAN backend (which would need a different
// implementation per vendor peripheral API) this one source file works
// unchanged on any Arduino-framework board. Only plain digitalWrite/
// pinMode/delayMicroseconds/micros() are used — no Teensy-specific API
// (see backend_stepdir.cpp's Pulse() for the one place this mattered:
// digitalWriteFast is a Teensyduino built-in, not universal Arduino-core
// API).
//
// Depends on ChannelState from channel_table.h — that file stays
// per-firmware-image (channel *count* is a compile-time, per-image fact,
// docs/BOARD_LAYER_RFC.md §7.5), so this is an implicit structural
// contract: any firmware wanting this backend must define a
// channel_table.h whose ChannelState has a `config` field of type
// jw1_configure_step_dir_t plus the pins_configured/enabled/target_mode/
// target_value/position_steps/last_step_us fields this file reads and
// writes. firmware/teensy/41/src/channel_table.h is the reference shape.
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
