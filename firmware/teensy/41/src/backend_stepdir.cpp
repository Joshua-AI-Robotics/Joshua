#include "backend_stepdir.h"

#include <Arduino.h>
#include <math.h>

namespace {

// Minimum microseconds between pulses for the channel's configured
// max_pulse_rate_hz; 0 (unconfigured) is treated as "not ready to step".
unsigned long MinPulseIntervalUs(const ChannelState& channel) {
  if (channel.config.max_pulse_rate_hz == 0) {
    return 0;
  }
  return 1000000UL / channel.config.max_pulse_rate_hz;
}

void Pulse(ChannelState* channel, bool dir_positive) {
  const bool dir_pin_level = channel->config.invert_dir ? !dir_positive : dir_positive;
  digitalWrite(channel->config.dir_pin, dir_pin_level ? HIGH : LOW);
  digitalWriteFast(channel->config.step_pin, HIGH);
  // 20us, not 2us: Teensy 4.1 drives 3.3V logic into a TB6600 opto-isolated
  // input commonly spec'd for 5V. A too-short HIGH time may not give the
  // opto's LED enough time to fully turn on at reduced drive current —
  // widened after a real hardware pass where the firmware/wire protocol
  // and the TB6600's ENA gating were confirmed correct (position tracked
  // exactly as commanded) but the motor still didn't respond.
  delayMicroseconds(20);
  digitalWriteFast(channel->config.step_pin, LOW);
  channel->position_steps += dir_positive ? 1 : -1;
  channel->last_step_us = micros();
}

}  // namespace

void StepDirInit(ChannelState* channel) {
  channel->pins_configured = false;
  channel->enabled = false;
}

void StepDirConfigure(ChannelState* channel, const jw1_configure_step_dir_t* config) {
  channel->config = *config;
  pinMode(channel->config.step_pin, OUTPUT);
  pinMode(channel->config.dir_pin, OUTPUT);
  pinMode(channel->config.enable_pin, OUTPUT);
  digitalWrite(channel->config.step_pin, LOW);
  digitalWrite(channel->config.dir_pin, LOW);
  channel->pins_configured = true;
  StepDirDisable(channel);  // Safe default until an explicit Enable() arrives.
}

void StepDirEnable(ChannelState* channel) {
  if (!channel->pins_configured) {
    return;  // No pins to drive yet — wait for CONFIGURE_CHANNEL.
  }
  const bool active_level = channel->config.enable_active_low ? LOW : HIGH;
  digitalWrite(channel->config.enable_pin, active_level);
  channel->enabled = true;
}

void StepDirDisable(ChannelState* channel) {
  channel->enabled = false;
  if (!channel->pins_configured) {
    return;  // No pins to drive yet — nothing to write.
  }
  const bool inactive_level = channel->config.enable_active_low ? HIGH : LOW;
  digitalWrite(channel->config.enable_pin, inactive_level);
}

void StepDirSetTarget(ChannelState* channel, jw1_mode_t mode, float value) {
  channel->target_mode = mode;
  channel->target_value = value;
}

void StepDirService(ChannelState* channel) {
  if (!channel->enabled) {
    return;
  }
  const unsigned long min_interval_us = MinPulseIntervalUs(*channel);
  if (min_interval_us == 0) {
    return;  // Not yet configured via CONFIGURE_CHANNEL.
  }
  if (micros() - channel->last_step_us < min_interval_us) {
    return;  // Throttled to max_pulse_rate_hz.
  }

  switch (channel->target_mode) {
    case JW1_MODE_POSITION: {
      const long target_steps = lroundf(channel->target_value);
      if (channel->position_steps == target_steps) {
        return;
      }
      Pulse(channel, target_steps > channel->position_steps);
      return;
    }
    case JW1_MODE_VELOCITY: {
      if (channel->target_value == 0.0f) {
        return;
      }
      Pulse(channel, channel->target_value > 0.0f);
      return;
    }
    case JW1_MODE_TORQUE:
    default:
      return;  // No torque target on an open-loop STEP_DIR channel.
  }
}
