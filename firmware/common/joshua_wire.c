// Implements the stateless JoshuaWire codec shared by host and firmware:
// frame encoding/decoding, CRC and correlation-field comparison. Session
// lifetime, request history and command execution are handled by its callers.
#include "joshua_wire.h"

#include <string.h>

static uint16_t crc16(const uint8_t* data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; ++i) {
    crc ^= (uint16_t)data[i] << 8;
    for (int bit = 0; bit < 8; ++bit) {
      crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
    }
  }
  return crc;
}

static void put_u32(uint8_t* out, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) out[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t get_u32(const uint8_t* in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}

int jw_encode_frame(uint8_t* buf,
                    size_t cap,
                    uint32_t session_id,
                    uint32_t message_id,
                    uint8_t cmd,
                    uint8_t channel,
                    const uint8_t* payload,
                    uint8_t payload_len) {
  const size_t total = JW_FRAME_OVERHEAD + (size_t)payload_len;
  if (buf == NULL || session_id == 0 || message_id == 0 || payload_len > JW_MAX_PAYLOAD_LEN ||
      cap < total || (payload_len != 0 && payload == NULL)) {
    return -1;
  }
  buf[0] = JW_SYNC_BYTE;
  buf[JW_LENGTH_OFFSET] = JW_HEADER_BODY_LEN + payload_len;
  buf[2] = JW_PROTO_VERSION;
  put_u32(buf + 3, session_id);
  put_u32(buf + 7, message_id);
  buf[11] = cmd;
  buf[12] = channel;
  if (payload_len != 0) memcpy(buf + 13, payload, payload_len);
  const uint16_t crc = crc16(buf + JW_LENGTH_PREFIX_LEN, buf[JW_LENGTH_OFFSET]);
  buf[total - JW_CRC_LEN] = (uint8_t)crc;
  buf[total - 1] = (uint8_t)(crc >> 8);
  return (int)total;
}

int jw_decode_frame(const uint8_t* buf, size_t len, jw_frame_t* out) {
  if (buf == NULL || out == NULL || len < JW_MIN_FRAME_LEN || len > JW_MAX_FRAME_LEN ||
      buf[0] != JW_SYNC_BYTE || buf[2] != JW_PROTO_VERSION ||
      buf[JW_LENGTH_OFFSET] < JW_HEADER_BODY_LEN ||
      (size_t)buf[JW_LENGTH_OFFSET] + JW_LENGTH_FIELD_OVERHEAD != len) {
    return -1;
  }
  const uint16_t crc = (uint16_t)buf[len - JW_CRC_LEN] | ((uint16_t)buf[len - 1] << 8);
  if (crc16(buf + JW_LENGTH_PREFIX_LEN, buf[JW_LENGTH_OFFSET]) != crc || get_u32(buf + 3) == 0 ||
      get_u32(buf + 7) == 0) {
    return -1;
  }
  out->proto_ver = buf[2];
  out->session_id = get_u32(buf + 3);
  out->message_id = get_u32(buf + 7);
  out->cmd = buf[11];
  out->channel = buf[12];
  out->payload = buf + 13;
  out->payload_len = buf[JW_LENGTH_OFFSET] - JW_HEADER_BODY_LEN;
  return 0;
}

int jw_encode_response(uint8_t* buf,
                       size_t cap,
                       const jw_frame_t* request,
                       const uint8_t* payload,
                       uint8_t payload_len) {
  if (request == NULL || request->proto_ver != JW_PROTO_VERSION) return -1;
  return jw_encode_frame(buf,
                         cap,
                         request->session_id,
                         request->message_id,
                         request->cmd,
                         request->channel,
                         payload,
                         payload_len);
}

int jw_response_matches(const jw_frame_t* request, const jw_frame_t* response) {
  return request != NULL && response != NULL && request->proto_ver == JW_PROTO_VERSION &&
         response->proto_ver == JW_PROTO_VERSION && request->session_id != 0 &&
         request->message_id != 0 && request->session_id == response->session_id &&
         request->message_id == response->message_id && request->cmd == response->cmd &&
         request->channel == response->channel;
}
