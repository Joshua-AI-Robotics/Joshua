// Version-neutral payload serialization shared by host and MCU commands.
// This module owns byte layouts only: no framing, sessions, transport or GPIO.
#include "joshua_wire_commands.h"

#include <string.h>

static void put_u16(uint8_t* out, uint16_t value) {
  if (out == NULL) return;
  out[0] = (uint8_t)value;
  out[1] = (uint8_t)(value >> 8);
}
static uint16_t get_u16(const uint8_t* in) {
  if (in == NULL) return 0;
  return (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
}
static void put_u32(uint8_t* out, uint32_t value) {
  if (out == NULL) return;
  for (int i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (8 * i));
}
static uint32_t get_u32(const uint8_t* in) {
  if (in == NULL) return 0;
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}
static void put_float(uint8_t* out, float value) {
  if (out == NULL) return;
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  put_u32(out, bits);
}
static float get_float(const uint8_t* in) {
  if (in == NULL) return 0.0f;
  uint32_t bits = get_u32(in);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

int jw_encode_status_payload(jw_status_t status, uint8_t* out, size_t cap) {
  if (out == NULL || cap < JW_STATUS_RESPONSE_PAYLOAD_LEN) return -1;
  out[0] = (uint8_t)status;
  return JW_STATUS_RESPONSE_PAYLOAD_LEN;
}
jw_result_t jw_decode_status_payload(const uint8_t* data, size_t len, jw_status_t* out) {
  if (data == NULL || out == NULL || len != JW_STATUS_RESPONSE_PAYLOAD_LEN) return JW_RESULT_ERROR;
  *out = (jw_status_t)data[0];
  return JW_RESULT_OK;
}
int jw_encode_identify_payload(uint8_t* out, size_t cap, const jw_identify_response_t* value) {
  if (out == NULL || value == NULL || cap < JW_IDENTIFY_RESPONSE_PAYLOAD_LEN ||
      value->n_channels > JW_MAX_CHANNELS)
    return -1;
  out[0] = (uint8_t)value->board_id;
  memcpy(out + 1, value->fw_name, JW_FW_NAME_LEN);
  out[1 + JW_FW_NAME_LEN] = value->n_channels;
  for (uint8_t i = 0; i < JW_MAX_CHANNELS; ++i) {
    out[2 + JW_FW_NAME_LEN + i] =
        (uint8_t)(i < value->n_channels ? value->channel_drives[i] : JW_DRIVE_INVALID);
  }
  return JW_IDENTIFY_RESPONSE_PAYLOAD_LEN;
}
jw_result_t jw_decode_identify_payload(const uint8_t* data,
                                       size_t len,
                                       jw_identify_response_t* out) {
  if (data == NULL || out == NULL || len != JW_IDENTIFY_RESPONSE_PAYLOAD_LEN ||
      data[1 + JW_FW_NAME_LEN] > JW_MAX_CHANNELS)
    return JW_RESULT_ERROR;
  out->board_id = (jw_board_id_t)data[0];
  memcpy(out->fw_name, data + 1, JW_FW_NAME_LEN);
  out->n_channels = data[1 + JW_FW_NAME_LEN];
  for (uint8_t i = 0; i < JW_MAX_CHANNELS; ++i) {
    out->channel_drives[i] =
        i < out->n_channels ? (jw_drive_t)data[2 + JW_FW_NAME_LEN + i] : JW_DRIVE_INVALID;
  }
  return JW_RESULT_OK;
}
int jw_encode_feedback_payload(uint8_t* out, size_t cap, const jw_feedback_t* value) {
  if (out == NULL || value == NULL || cap < JW_FEEDBACK_RESPONSE_PAYLOAD_LEN) return -1;
  put_float(out, value->position);
  put_float(out + 4, value->velocity);
  put_u16(out + 8, value->fault_flags);
  return JW_FEEDBACK_RESPONSE_PAYLOAD_LEN;
}
jw_result_t jw_decode_feedback_payload(const uint8_t* data, size_t len, jw_feedback_t* out) {
  if (data == NULL || out == NULL || len != JW_FEEDBACK_RESPONSE_PAYLOAD_LEN)
    return JW_RESULT_ERROR;
  out->position = get_float(data);
  out->velocity = get_float(data + 4);
  out->fault_flags = get_u16(data + 8);
  return JW_RESULT_OK;
}
int jw_encode_set_target_payload(uint8_t* out, size_t cap, jw_mode_t mode, float value) {
  if (out == NULL || cap < JW_SET_TARGET_PAYLOAD_LEN) return -1;
  out[0] = (uint8_t)mode;
  put_float(out + 1, value);
  return JW_SET_TARGET_PAYLOAD_LEN;
}
jw_result_t jw_decode_set_target_payload(const uint8_t* data, size_t len, jw_set_target_t* out) {
  if (data == NULL || out == NULL || len != JW_SET_TARGET_PAYLOAD_LEN) return JW_RESULT_ERROR;
  out->mode = (jw_mode_t)data[0];
  out->value = get_float(data + 1);
  return JW_RESULT_OK;
}
int jw_encode_configure_step_dir_payload(uint8_t* out,
                                         size_t cap,
                                         const jw_configure_step_dir_t* value) {
  if (out == NULL || value == NULL || cap < JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN) return -1;
  put_u32(out, value->max_pulse_rate_hz);
  out[4] = value->invert_dir ? 1 : 0;
  out[5] = value->enable_active_low ? 1 : 0;
  out[6] = value->step_pin;
  out[7] = value->dir_pin;
  out[8] = value->enable_pin;
  put_u16(out + 9, value->step_pulse_width_us);
  return JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN;
}
jw_result_t jw_decode_configure_step_dir_payload(const uint8_t* data,
                                                 size_t len,
                                                 jw_configure_step_dir_t* out) {
  if (data == NULL || out == NULL || len != JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN)
    return JW_RESULT_ERROR;
  out->max_pulse_rate_hz = get_u32(data);
  out->invert_dir = data[4];
  out->enable_active_low = data[5];
  out->step_pin = data[6];
  out->dir_pin = data[7];
  out->enable_pin = data[8];
  out->step_pulse_width_us = get_u16(data + 9);
  return JW_RESULT_OK;
}
