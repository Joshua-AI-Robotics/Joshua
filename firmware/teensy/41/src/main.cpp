// firmware/teensy/41 — Joshua's own firmware protocol (joshua_wire_v1) on a
// Teensy 4.1, driving TB6600 (or any STEP/DIR drive) channels declared in
// channel_table.c (docs/BOARD_LAYER_RFC.md §7). No joint names, no limits,
// no robot identity here — this binary is "N STEP_DIR channels speaking
// joshua_wire_v1", nothing else; per-robot facts live in the host .pbtxt.
#include <Arduino.h>
#include <string.h>

#include "backend_stepdir.h"
#include "channel_table.h"
#include "joshua_wire_v1.h"
#include "transport_serial.h"

namespace {

ChannelState* LookupChannel(uint8_t index) {
  if (index >= g_num_channels) {
    return nullptr;
  }
  return &g_channels[index];
}

void RespondStatus(uint8_t cmd, uint8_t channel, jw1_status_t status) {
  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_status_response(buf, sizeof(buf), cmd, channel, status);
  if (len > 0) {
    TransportWriteFrame(buf, static_cast<size_t>(len));
  }
}

void HandleIdentify() {
  jw1_identify_response_t response;
  memset(&response, 0, sizeof(response));
  response.board_id = JW1_BOARD_TEENSY41;
  // fw_name intentionally left zeroed: no host code checks it
  // (docs/BOARD_LAYER_RFC.md §7.5) — proto_ver + per-channel drive type
  // are the real compatibility gate.
  response.n_channels = g_num_channels;
  for (uint8_t i = 0; i < g_num_channels; i++) {
    response.channel_drives[i] = JW1_DRIVE_STEP_DIR;
  }

  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_identify_response(buf, sizeof(buf), &response);
  if (len > 0) {
    TransportWriteFrame(buf, static_cast<size_t>(len));
  }
}

void HandleConfigureChannel(const jw1_frame_t& frame) {
  ChannelState* channel = LookupChannel(frame.channel);
  jw1_configure_step_dir_t config;
  if (channel == nullptr || jw1_decode_configure_channel_step_dir(&frame, &config) != 0) {
    RespondStatus(JW1_CMD_CONFIGURE_CHANNEL, frame.channel, JW1_STATUS_ERROR);
    return;
  }
  StepDirConfigure(channel, &config);
  RespondStatus(JW1_CMD_CONFIGURE_CHANNEL, frame.channel, JW1_STATUS_OK);
}

void HandleSetTarget(const jw1_frame_t& frame) {
  ChannelState* channel = LookupChannel(frame.channel);
  jw1_set_target_t target;
  if (channel == nullptr || jw1_decode_set_target(&frame, &target) != 0) {
    RespondStatus(JW1_CMD_SET_TARGET, frame.channel, JW1_STATUS_ERROR);
    return;
  }
  StepDirSetTarget(channel, target.mode, target.value);
  RespondStatus(JW1_CMD_SET_TARGET, frame.channel, JW1_STATUS_OK);
}

void HandleGetFeedback(const jw1_frame_t& frame) {
  ChannelState* channel = LookupChannel(frame.channel);
  if (channel == nullptr) {
    RespondStatus(JW1_CMD_GET_FEEDBACK, frame.channel, JW1_STATUS_ERROR);
    return;
  }
  jw1_feedback_t feedback;
  feedback.position = static_cast<float>(channel->position_steps);
  // Open-loop: no tachometer. Reports the last commanded velocity target
  // when in velocity mode, 0 otherwise — an approximation, not a
  // measurement.
  feedback.velocity = channel->target_mode == JW1_MODE_VELOCITY ? channel->target_value : 0.0f;
  feedback.fault_flags = 0;

  uint8_t buf[JW1_MAX_FRAME_LEN];
  const int len = jw1_encode_feedback_response(buf, sizeof(buf), frame.channel, &feedback);
  if (len > 0) {
    TransportWriteFrame(buf, static_cast<size_t>(len));
  }
}

void HandleEnable(const jw1_frame_t& frame) {
  ChannelState* channel = LookupChannel(frame.channel);
  if (channel == nullptr) {
    RespondStatus(JW1_CMD_ENABLE, frame.channel, JW1_STATUS_ERROR);
    return;
  }
  StepDirEnable(channel);
  RespondStatus(JW1_CMD_ENABLE, frame.channel, JW1_STATUS_OK);
}

void HandleDisable(const jw1_frame_t& frame) {
  ChannelState* channel = LookupChannel(frame.channel);
  if (channel == nullptr) {
    RespondStatus(JW1_CMD_DISABLE, frame.channel, JW1_STATUS_ERROR);
    return;
  }
  StepDirDisable(channel);
  RespondStatus(JW1_CMD_DISABLE, frame.channel, JW1_STATUS_OK);
}

void HandleEstop(const jw1_frame_t& frame) {
  for (uint8_t i = 0; i < g_num_channels; i++) {
    StepDirDisable(&g_channels[i]);
  }
  RespondStatus(JW1_CMD_ESTOP, frame.channel, JW1_STATUS_OK);
}

void Dispatch(const jw1_frame_t& frame) {
  switch (frame.cmd) {
    case JW1_CMD_IDENTIFY:
      HandleIdentify();
      return;
    case JW1_CMD_CONFIGURE_CHANNEL:
      HandleConfigureChannel(frame);
      return;
    case JW1_CMD_SET_TARGET:
      HandleSetTarget(frame);
      return;
    case JW1_CMD_GET_FEEDBACK:
      HandleGetFeedback(frame);
      return;
    case JW1_CMD_ENABLE:
      HandleEnable(frame);
      return;
    case JW1_CMD_DISABLE:
      HandleDisable(frame);
      return;
    case JW1_CMD_ESTOP:
      HandleEstop(frame);
      return;
    default:
      RespondStatus(frame.cmd, frame.channel, JW1_STATUS_UNSUPPORTED);
      return;
  }
}

}  // namespace

void setup() {
  TransportInit();
  for (uint8_t i = 0; i < g_num_channels; i++) {
    StepDirInit(&g_channels[i]);
  }
}

void loop() {
  uint8_t frame_buf[JW1_MAX_FRAME_LEN];
  jw1_frame_t frame;
  if (TransportReadFrame(&frame, frame_buf, sizeof(frame_buf))) {
    Dispatch(frame);
  }
  for (uint8_t i = 0; i < g_num_channels; i++) {
    StepDirService(&g_channels[i]);
  }
}
