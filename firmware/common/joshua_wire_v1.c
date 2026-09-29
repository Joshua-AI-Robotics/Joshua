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
  if (len < 4 || buf[0] != JW1_SYNC_BYTE || buf[2] != JW1_PROTO_VERSION ||
      len > JW1_MAX_FRAME_LEN) {
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
  return jw1_encode_frame(buf, cap, JW_CMD_IDENTIFY, JW_CHANNEL_NONE, NULL, 0);
}

int jw1_encode_configure_channel_step_dir(uint8_t* buf,
                                          size_t cap,
                                          uint8_t channel,
                                          const jw_configure_step_dir_t* config) {
  uint8_t payload[JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN];
  const int size = jw_encode_configure_step_dir_payload(payload, sizeof(payload), config);
  if (size < 0) return -1;
  return jw1_encode_frame(buf, cap, JW_CMD_CONFIGURE_CHANNEL, channel, payload, (uint8_t)size);
}

int jw1_encode_set_target(uint8_t* buf, size_t cap, uint8_t channel, jw_mode_t mode, float value) {
  uint8_t payload[JW_SET_TARGET_PAYLOAD_LEN];
  const int size = jw_encode_set_target_payload(payload, sizeof(payload), mode, value);
  if (size < 0) return -1;
  return jw1_encode_frame(buf, cap, JW_CMD_SET_TARGET, channel, payload, (uint8_t)size);
}

int jw1_encode_get_feedback_request(uint8_t* buf, size_t cap, uint8_t channel) {
  return jw1_encode_frame(buf, cap, JW_CMD_GET_FEEDBACK, channel, NULL, 0);
}

int jw1_encode_enable(uint8_t* buf, size_t cap, uint8_t channel) {
  return jw1_encode_frame(buf, cap, JW_CMD_ENABLE, channel, NULL, 0);
}

int jw1_encode_disable(uint8_t* buf, size_t cap, uint8_t channel) {
  return jw1_encode_frame(buf, cap, JW_CMD_DISABLE, channel, NULL, 0);
}

int jw1_encode_estop(uint8_t* buf, size_t cap) {
  return jw1_encode_frame(buf, cap, JW_CMD_ESTOP, JW_CHANNEL_NONE, NULL, 0);
}

int jw1_encode_status_response(
    uint8_t* buf, size_t cap, uint8_t cmd, uint8_t channel, jw_status_t status) {
  uint8_t payload[JW_STATUS_RESPONSE_PAYLOAD_LEN];
  const int size = jw_encode_status_payload(payload, sizeof(payload), status);
  if (size < 0) return -1;
  return jw1_encode_frame(buf, cap, cmd, channel, payload, (uint8_t)size);
}

int jw1_encode_identify_response(uint8_t* buf, size_t cap, const jw_identify_response_t* response) {
  uint8_t payload[JW_IDENTIFY_RESPONSE_PAYLOAD_LEN];
  const int size = jw_encode_identify_payload(payload, sizeof(payload), response);
  if (size < 0) return -1;
  return jw1_encode_frame(buf, cap, JW_CMD_IDENTIFY, JW_CHANNEL_NONE, payload, (uint8_t)size);
}

int jw1_encode_feedback_response(uint8_t* buf,
                                 size_t cap,
                                 uint8_t channel,
                                 const jw_feedback_t* feedback) {
  uint8_t payload[JW_FEEDBACK_RESPONSE_PAYLOAD_LEN];
  const int size = jw_encode_feedback_payload(payload, sizeof(payload), feedback);
  if (size < 0) return -1;
  return jw1_encode_frame(buf, cap, JW_CMD_GET_FEEDBACK, channel, payload, (uint8_t)size);
}

int jw1_decode_identify_response(const jw1_frame_t* frame, jw_identify_response_t* out) {
  if (frame == NULL || frame->cmd != JW_CMD_IDENTIFY) return -1;
  return jw_decode_identify_payload(frame->payload, frame->payload_len, out);
}

int jw1_decode_feedback_response(const jw1_frame_t* frame, jw_feedback_t* out) {
  if (frame == NULL || frame->cmd != JW_CMD_GET_FEEDBACK) return -1;
  return jw_decode_feedback_payload(frame->payload, frame->payload_len, out);
}

int jw1_decode_status_response(const jw1_frame_t* frame, jw_status_t* out) {
  if (frame == NULL) return -1;
  return jw_decode_status_payload(frame->payload, frame->payload_len, out);
}

int jw1_decode_set_target(const jw1_frame_t* frame, jw_set_target_t* out) {
  if (frame == NULL || frame->cmd != JW_CMD_SET_TARGET) return -1;
  return jw_decode_set_target_payload(frame->payload, frame->payload_len, out);
}

int jw1_decode_configure_channel_step_dir(const jw1_frame_t* frame, jw_configure_step_dir_t* out) {
  if (frame == NULL || frame->cmd != JW_CMD_CONFIGURE_CHANNEL) return -1;
  return jw_decode_configure_step_dir_payload(frame->payload, frame->payload_len, out);
}
