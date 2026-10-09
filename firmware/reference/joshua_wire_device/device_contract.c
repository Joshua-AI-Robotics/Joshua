#include "device_contract.h"

static void put16(uint8_t* p, uint16_t value) {
  p[0] = (uint8_t)value;
  p[1] = (uint8_t)(value >> 8);
}

static uint16_t get16(const uint8_t* p) {
  return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static void put32(uint8_t* p, uint32_t value) {
  for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (8 * i));
}

static uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int jwd_encode_info(uint8_t* out, size_t cap, const jwd_info_t* value) {
  if (out == NULL || value == NULL || cap < JWD_INFO_SIZE) return -1;
  out[0] = value->contract_version;
  out[1] = value->wire_version;
  out[2] = value->max_payload;
  out[3] = value->channel_id_bytes;
  put16(out + 4, value->descriptor_count);
  put32(out + 6, value->vendor_id);
  put32(out + 10, value->product_id);
  put32(out + 14, value->firmware_revision);
  put32(out + 18, value->boot_id);
  return JWD_INFO_SIZE;
}

jw_result_t jwd_decode_info(const uint8_t* data, size_t len, jwd_info_t* value) {
  if (data == NULL || value == NULL || len != JWD_INFO_SIZE) return JW_RESULT_ERROR;
  value->contract_version = data[0];
  value->wire_version = data[1];
  value->max_payload = data[2];
  value->channel_id_bytes = data[3];
  value->descriptor_count = get16(data + 4);
  value->vendor_id = get32(data + 6);
  value->product_id = get32(data + 10);
  value->firmware_revision = get32(data + 14);
  value->boot_id = get32(data + 18);
  return JW_RESULT_OK;
}

int jwd_encode_descriptor(uint8_t* out, size_t cap, const jwd_descriptor_t* value) {
  if (out == NULL || value == NULL || cap < JWD_DESCRIPTOR_SIZE) return -1;
  out[0] = value->channel;
  put16(out + 1, value->physical_device);
  put32(out + 3, value->profile);
  put16(out + 7, value->profile_version);
  put32(out + 9, value->operations);
  put16(out + 13, value->safety_group);
  return JWD_DESCRIPTOR_SIZE;
}

jw_result_t jwd_decode_descriptor(const uint8_t* data, size_t len, jwd_descriptor_t* value) {
  if (data == NULL || value == NULL || len != JWD_DESCRIPTOR_SIZE) return JW_RESULT_ERROR;
  value->channel = data[0];
  value->physical_device = get16(data + 1);
  value->profile = get32(data + 3);
  value->profile_version = get16(data + 7);
  value->operations = get32(data + 9);
  value->safety_group = get16(data + 13);
  return JW_RESULT_OK;
}

int jwd_encode_state(uint8_t* out, size_t cap, const jwd_state_t* value) {
  if (out == NULL || value == NULL || cap < JWD_STATE_SIZE) return -1;
  out[0] = value->channel;
  out[1] = value->state;
  out[2] = value->reason;
  out[3] = value->stop_evidence;
  out[4] = value->recovery;
  put32(out + 5, value->generation);
  put16(out + 9, value->safety_group);
  return JWD_STATE_SIZE;
}

jw_result_t jwd_decode_state(const uint8_t* data, size_t len, jwd_state_t* value) {
  if (data == NULL || value == NULL || len != JWD_STATE_SIZE) return JW_RESULT_ERROR;
  value->channel = data[0];
  value->state = data[1];
  value->reason = data[2];
  value->stop_evidence = data[3];
  value->recovery = data[4];
  value->generation = get32(data + 5);
  value->safety_group = get16(data + 9);
  return JW_RESULT_OK;
}
