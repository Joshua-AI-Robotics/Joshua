// Same-directory quote-include, not a repo-root-relative Bazel-style path:
// PlatformIO's library builder (firmware/teensy/41/platformio.ini) resolves
// includes relative to this library's own directory, unlike Bazel's
// workspace-rooted include paths (docs/BOARD_LAYER_RFC.md §7.3).
#include "joshua_wire_v1.h"

#include <string.h>

// CRC-16/CCITT-FALSE: poly 0x1021, init 0xFFFF, no reflection, no output
// xor. Bit-banged (no table) — cheap enough for the frame sizes here and
// keeps flash footprint small on the smallest firmware target.
static uint16_t jw1_crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int bit = 0; bit < 8; bit++) {
      if (crc & 0x8000) {
        crc = (uint16_t)((crc << 1) ^ 0x1021);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

static void jw1_put_u16le(uint8_t* out, uint16_t value) {
  out[0] = (uint8_t)(value & 0xFF);
  out[1] = (uint8_t)((value >> 8) & 0xFF);
}

static uint16_t jw1_get_u16le(const uint8_t* in) {
  return (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
}

static void jw1_put_u32le(uint8_t* out, uint32_t value) {
  out[0] = (uint8_t)(value & 0xFF);
  out[1] = (uint8_t)((value >> 8) & 0xFF);
  out[2] = (uint8_t)((value >> 16) & 0xFF);
  out[3] = (uint8_t)((value >> 24) & 0xFF);
}

static uint32_t jw1_get_u32le(const uint8_t* in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}

static void jw1_put_f32le(uint8_t* out, float value) {
  uint32_t bits;
  memcpy(&bits, &value, sizeof(bits));
  jw1_put_u32le(out, bits);
}

static float jw1_get_f32le(const uint8_t* in) {
  uint32_t bits = jw1_get_u32le(in);
  float value;
  memcpy(&value, &bits, sizeof(value));
  return value;
}

int jw1_encode_frame(uint8_t* buf,
                     size_t cap,
                     uint8_t cmd,
                     uint8_t channel,
                     const uint8_t* payload,
                     uint8_t payload_len) {
  if (buf == NULL || (payload_len > 0 && payload == NULL)) {
    return -1;
  }
  if (payload_len > JW1_MAX_PAYLOAD_LEN) {
    return -1;
  }
  const uint8_t len = (uint8_t)(3 + payload_len);
  const size_t total = (size_t)(2 + len + 2);
  if (cap < total) {
    return -1;
  }

  buf[0] = JW1_SYNC_BYTE;
  buf[1] = len;
  buf[2] = JW1_PROTO_VERSION;
  buf[3] = cmd;
  buf[4] = channel;
  if (payload_len > 0) {
    memcpy(buf + 5, payload, payload_len);
  }

  const uint16_t crc = jw1_crc16(buf + 2, len);
  jw1_put_u16le(buf + 2 + len, crc);
  return (int)total;
}

int jw1_decode_frame(const uint8_t* buf, size_t len, jw1_frame_t* out) {
  if (buf == NULL || out == NULL) {
    return -1;
  }
  if (len < 4 || buf[0] != JW1_SYNC_BYTE) {
    return -1;
  }
  const uint8_t frame_len = buf[1];
  if (frame_len < 3 || (size_t)(2 + frame_len + 2) != len) {
    return -1;
  }

  const uint16_t expected_crc = jw1_crc16(buf + 2, frame_len);
  const uint16_t actual_crc = jw1_get_u16le(buf + 2 + frame_len);
  if (expected_crc != actual_crc) {
    return -1;
  }

  out->proto_ver = buf[2];
  out->cmd = buf[3];
  out->channel = buf[4];
  out->payload = buf + 5;
  out->payload_len = (uint8_t)(frame_len - 3);
  return 0;
}

int jw1_encode_identify_request(uint8_t* buf, size_t cap) {
  return jw1_encode_frame(buf, cap, JW1_CMD_IDENTIFY, JW1_CHANNEL_NONE, NULL, 0);
}

int jw1_encode_configure_channel_step_dir(uint8_t* buf,
                                          size_t cap,
                                          uint8_t channel,
                                          const jw1_configure_step_dir_t* config) {
  if (config == NULL) {
    return -1;
  }
  uint8_t payload[11] = {0};
  jw1_put_u32le(payload, config->max_pulse_rate_hz);
  payload[4] = config->invert_dir ? 1 : 0;
  payload[5] = config->enable_active_low ? 1 : 0;
  payload[6] = config->step_pin;
  payload[7] = config->dir_pin;
  payload[8] = config->enable_pin;
  jw1_put_u16le(payload + 9, config->step_pulse_width_us);
  return jw1_encode_frame(buf, cap, JW1_CMD_CONFIGURE_CHANNEL, channel, payload, sizeof(payload));
}

int jw1_encode_set_target(uint8_t* buf, size_t cap, uint8_t channel, jw1_mode_t mode, float value) {
  uint8_t payload[5] = {0};
  payload[0] = (uint8_t)mode;
  jw1_put_f32le(payload + 1, value);
  return jw1_encode_frame(buf, cap, JW1_CMD_SET_TARGET, channel, payload, sizeof(payload));
}

int jw1_encode_get_feedback_request(uint8_t* buf, size_t cap, uint8_t channel) {
  return jw1_encode_frame(buf, cap, JW1_CMD_GET_FEEDBACK, channel, NULL, 0);
}

int jw1_encode_enable(uint8_t* buf, size_t cap, uint8_t channel) {
  return jw1_encode_frame(buf, cap, JW1_CMD_ENABLE, channel, NULL, 0);
}

int jw1_encode_disable(uint8_t* buf, size_t cap, uint8_t channel) {
  return jw1_encode_frame(buf, cap, JW1_CMD_DISABLE, channel, NULL, 0);
}

int jw1_encode_estop(uint8_t* buf, size_t cap) {
  return jw1_encode_frame(buf, cap, JW1_CMD_ESTOP, JW1_CHANNEL_NONE, NULL, 0);
}

int jw1_encode_status_response(
    uint8_t* buf, size_t cap, uint8_t cmd, uint8_t channel, jw1_status_t status) {
  const uint8_t payload[1] = {(uint8_t)status};
  return jw1_encode_frame(buf, cap, cmd, channel, payload, sizeof(payload));
}

int jw1_encode_identify_response(uint8_t* buf,
                                 size_t cap,
                                 const jw1_identify_response_t* response) {
  if (response == NULL) {
    return -1;
  }
  if (response->n_channels > JW1_MAX_CHANNELS) {
    return -1;
  }
  // Fixed-size payload (JW1_IDENTIFY_RESPONSE_PAYLOAD_LEN) so a host atop a
  // fixed-size-read transport can size its read before it knows
  // n_channels; slots beyond n_channels are padded JW1_DRIVE_INVALID.
  uint8_t payload[JW1_IDENTIFY_RESPONSE_PAYLOAD_LEN] = {0};
  size_t offset = 0;
  payload[offset++] = (uint8_t)response->board_id;
  memcpy(payload + offset, response->fw_name, JW1_FW_NAME_LEN);
  offset += JW1_FW_NAME_LEN;
  payload[offset++] = response->n_channels;
  for (uint8_t i = 0; i < JW1_MAX_CHANNELS; i++) {
    payload[offset++] =
        (uint8_t)(i < response->n_channels ? response->channel_drives[i] : JW1_DRIVE_INVALID);
  }
  return jw1_encode_frame(buf, cap, JW1_CMD_IDENTIFY, JW1_CHANNEL_NONE, payload, (uint8_t)offset);
}

int jw1_encode_feedback_response(uint8_t* buf,
                                 size_t cap,
                                 uint8_t channel,
                                 const jw1_feedback_t* feedback) {
  if (feedback == NULL) {
    return -1;
  }
  uint8_t payload[10] = {0};
  jw1_put_f32le(payload, feedback->position);
  jw1_put_f32le(payload + 4, feedback->velocity);
  jw1_put_u16le(payload + 8, feedback->fault_flags);
  return jw1_encode_frame(buf, cap, JW1_CMD_GET_FEEDBACK, channel, payload, sizeof(payload));
}

int jw1_decode_identify_response(const jw1_frame_t* frame, jw1_identify_response_t* out) {
  if (frame == NULL || out == NULL) {
    return -1;
  }
  if (frame->cmd != JW1_CMD_IDENTIFY || frame->payload_len != JW1_IDENTIFY_RESPONSE_PAYLOAD_LEN) {
    return -1;
  }
  size_t offset = 0;
  out->board_id = (jw1_board_id_t)frame->payload[offset++];
  memcpy(out->fw_name, frame->payload + offset, JW1_FW_NAME_LEN);
  offset += JW1_FW_NAME_LEN;
  out->n_channels = frame->payload[offset++];
  if (out->n_channels > JW1_MAX_CHANNELS) {
    return -1;
  }
  for (uint8_t i = 0; i < out->n_channels; i++) {
    out->channel_drives[i] = (jw1_drive_t)frame->payload[offset + i];
  }
  return 0;
}

int jw1_decode_feedback_response(const jw1_frame_t* frame, jw1_feedback_t* out) {
  if (frame == NULL || out == NULL) {
    return -1;
  }
  if (frame->cmd != JW1_CMD_GET_FEEDBACK || frame->payload_len != 10) {
    return -1;
  }
  out->position = jw1_get_f32le(frame->payload);
  out->velocity = jw1_get_f32le(frame->payload + 4);
  out->fault_flags = jw1_get_u16le(frame->payload + 8);
  return 0;
}

int jw1_decode_status_response(const jw1_frame_t* frame, jw1_status_t* out) {
  if (frame == NULL || out == NULL) {
    return -1;
  }
  if (frame->payload_len != 1) {
    return -1;
  }
  *out = (jw1_status_t)frame->payload[0];
  return 0;
}

int jw1_decode_set_target(const jw1_frame_t* frame, jw1_set_target_t* out) {
  if (frame == NULL || out == NULL) {
    return -1;
  }
  if (frame->cmd != JW1_CMD_SET_TARGET || frame->payload_len != 5) {
    return -1;
  }
  out->mode = (jw1_mode_t)frame->payload[0];
  out->value = jw1_get_f32le(frame->payload + 1);
  return 0;
}

int jw1_decode_configure_channel_step_dir(const jw1_frame_t* frame, jw1_configure_step_dir_t* out) {
  if (frame == NULL || out == NULL) {
    return -1;
  }
  if (frame->cmd != JW1_CMD_CONFIGURE_CHANNEL || frame->payload_len != 11) {
    return -1;
  }
  out->max_pulse_rate_hz = jw1_get_u32le(frame->payload);
  out->invert_dir = frame->payload[4];
  out->enable_active_low = frame->payload[5];
  out->step_pin = frame->payload[6];
  out->dir_pin = frame->payload[7];
  out->enable_pin = frame->payload[8];
  out->step_pulse_width_us = jw1_get_u16le(frame->payload + 9);
  return 0;
}
