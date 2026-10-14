// Core payload layouts only; profile IDs and metadata remain opaque here.
#include "joshua_wire_commands.h"

#include <string.h>

static void put_u16(uint8_t* out, uint16_t v) {
  out[0] = (uint8_t)v;
  out[1] = (uint8_t)(v >> 8);
}
static uint16_t get_u16(const uint8_t* in) {
  return (uint16_t)(in[0] | ((uint16_t)in[1] << 8));
}
static void put_u32(uint8_t* out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get_u32(const uint8_t* in) {
  return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
         ((uint32_t)in[3] << 24);
}
static int valid_reply(jw_status_t s, jw_execution_t e, size_t len) {
  if (s < JW_STATUS_OK || s > JW_STATUS_OUTCOME_UNKNOWN || e > JW_EXECUTION_UNKNOWN || e < 0)
    return 0;
  if (s == JW_STATUS_OK) return e == JW_EXECUTION_ACCEPTED || e == JW_EXECUTION_COMPLETE;
  if (s == JW_STATUS_OUTCOME_UNKNOWN) return e == JW_EXECUTION_UNKNOWN && len == 0;
  if (s == JW_STATUS_ERROR) return e == JW_EXECUTION_COMPLETE;
  return e == JW_EXECUTION_REJECTED;
}
int jw_encode_reply_payload(
    uint8_t* out, size_t cap, jw_status_t s, jw_execution_t e, const uint8_t* data, size_t len) {
  if (out == NULL || len > JW_PAYLOAD_LIMIT - JW_REPLY_HEADER_LEN ||
      cap < JW_REPLY_HEADER_LEN + len || (len != 0 && data == NULL) || !valid_reply(s, e, len))
    return -1;
  // memmove permits callers to prepend a header in their own payload buffer.
  if (len != 0) memmove(out + JW_REPLY_HEADER_LEN, data, len);
  out[0] = (uint8_t)s;
  out[1] = (uint8_t)e;
  return (int)(JW_REPLY_HEADER_LEN + len);
}
jw_result_t jw_decode_reply_payload(const uint8_t* data, size_t len, jw_reply_t* out) {
  if (data == NULL || out == NULL || len < JW_REPLY_HEADER_LEN || len > JW_PAYLOAD_LIMIT ||
      !valid_reply((jw_status_t)data[0], (jw_execution_t)data[1], len - JW_REPLY_HEADER_LEN))
    return JW_RESULT_ERROR;
  const jw_reply_t v = {(jw_status_t)data[0],
                        (jw_execution_t)data[1],
                        data + JW_REPLY_HEADER_LEN,
                        len - JW_REPLY_HEADER_LEN};
  *out = v;
  return JW_RESULT_OK;
}
static int valid_identify(const jw_identify_response_t* v) {
  return v->boot_id != 0 && v->discovery_generation != 0 && v->function_count <= JW_MAX_FUNCTIONS &&
         v->max_frame_len == 96 && v->max_inflight == 1 && v->max_page_entries > 0 &&
         v->max_page_entries <= JW_DISCOVERY_PAGE_SIZE && v->max_handler_us > 0 &&
         v->protocol_min > 0 && v->protocol_min <= v->protocol_max &&
         (v->supported_planes & JW_PLANE_MESSAGE) != 0;
}
int jw_encode_identify_payload(uint8_t* out, size_t cap, const jw_identify_response_t* v) {
  if (out == NULL || v == NULL || cap < JW_IDENTIFY_PAYLOAD_LEN || !valid_identify(v)) return -1;
  put_u32(out, v->vendor_id);
  put_u32(out + 4, v->product_id);
  memcpy(out + 8, v->fw_name, JW_FW_NAME_LEN);
  put_u32(out + 24, v->boot_id);
  put_u32(out + 28, v->discovery_generation);
  put_u16(out + 32, v->function_count);
  put_u16(out + 34, v->max_frame_len);
  out[36] = v->max_inflight;
  out[37] = v->max_page_entries;
  put_u32(out + 38, v->max_handler_us);
  put_u32(out + 42, v->safety_flags);
  out[46] = v->protocol_min;
  out[47] = v->protocol_max;
  put_u32(out + 48, v->supported_planes);
  return JW_IDENTIFY_PAYLOAD_LEN;
}
jw_result_t jw_decode_identify_payload(const uint8_t* data,
                                       size_t len,
                                       jw_identify_response_t* out) {
  if (data == NULL || out == NULL || len != JW_IDENTIFY_PAYLOAD_LEN) return JW_RESULT_ERROR;
  jw_identify_response_t v;
  memset(&v, 0, sizeof(v));
  v.vendor_id = get_u32(data);
  v.product_id = get_u32(data + 4);
  memcpy(v.fw_name, data + 8, JW_FW_NAME_LEN);
  v.boot_id = get_u32(data + 24);
  v.discovery_generation = get_u32(data + 28);
  v.function_count = get_u16(data + 32);
  v.max_frame_len = get_u16(data + 34);
  v.max_inflight = data[36];
  v.max_page_entries = data[37];
  v.max_handler_us = get_u32(data + 38);
  v.safety_flags = get_u32(data + 42);
  v.protocol_min = data[46];
  v.protocol_max = data[47];
  v.supported_planes = get_u32(data + 48);
  if (!valid_identify(&v)) return JW_RESULT_ERROR;
  *out = v;
  return JW_RESULT_OK;
}
static int valid_function(const jw_function_descriptor_t* v) {
  return v->function_id != JW_CHANNEL_NONE && v->profile_id != 0 && v->profile_major != 0;
}
int jw_encode_function_descriptor(uint8_t* out, size_t cap, const jw_function_descriptor_t* v) {
  if (out == NULL || v == NULL || cap < JW_FUNCTION_DESCRIPTOR_LEN || !valid_function(v)) return -1;
  out[0] = v->function_id;
  put_u16(out + 1, v->device_id);
  put_u16(out + 3, v->profile_id);
  out[5] = v->profile_major;
  out[6] = v->profile_minor;
  put_u32(out + 7, v->operations);
  put_u16(out + 11, v->safety_domain);
  return JW_FUNCTION_DESCRIPTOR_LEN;
}
jw_result_t jw_decode_function_descriptor(const uint8_t* data,
                                          size_t len,
                                          jw_function_descriptor_t* out) {
  if (data == NULL || out == NULL || len != JW_FUNCTION_DESCRIPTOR_LEN) return JW_RESULT_ERROR;
  const jw_function_descriptor_t v = {data[0],
                                      get_u16(data + 1),
                                      get_u16(data + 3),
                                      data[5],
                                      data[6],
                                      get_u32(data + 7),
                                      get_u16(data + 11)};
  if (!valid_function(&v)) return JW_RESULT_ERROR;
  *out = v;
  return JW_RESULT_OK;
}
static int valid_page(const jw_discovery_page_t* v) {
  if (v->generation == 0 || v->total > JW_MAX_FUNCTIONS || v->count > JW_DISCOVERY_PAGE_SIZE ||
      v->count > v->total || (v->count == 0 && v->total != 0) ||
      (v->next_cursor != JW_CURSOR_END &&
       (v->count == 0 || v->next_cursor < v->count || v->next_cursor >= v->total)))
    return 0;
  for (uint8_t i = 0; i < v->count; ++i) {
    if (!valid_function(&v->functions[i])) return 0;
    for (uint8_t j = 0; j < i; ++j)
      if (v->functions[i].function_id == v->functions[j].function_id) return 0;
  }
  return 1;
}
int jw_encode_discovery_page(uint8_t* out, size_t cap, const jw_discovery_page_t* v) {
  if (out == NULL || v == NULL || !valid_page(v)) return -1;
  const size_t len = JW_DISCOVERY_HEADER_LEN + v->count * JW_FUNCTION_DESCRIPTOR_LEN;
  if (cap < len) return -1;
  put_u32(out, v->generation);
  put_u16(out + 4, v->total);
  put_u16(out + 6, v->next_cursor);
  out[8] = v->count;
  for (uint8_t i = 0; i < v->count; ++i)
    jw_encode_function_descriptor(out + JW_DISCOVERY_HEADER_LEN + i * JW_FUNCTION_DESCRIPTOR_LEN,
                                  JW_FUNCTION_DESCRIPTOR_LEN,
                                  &v->functions[i]);
  return (int)len;
}
jw_result_t jw_decode_discovery_page(const uint8_t* data, size_t len, jw_discovery_page_t* out) {
  if (data == NULL || out == NULL || len < JW_DISCOVERY_HEADER_LEN ||
      data[8] > JW_DISCOVERY_PAGE_SIZE ||
      len != JW_DISCOVERY_HEADER_LEN + (size_t)data[8] * JW_FUNCTION_DESCRIPTOR_LEN)
    return JW_RESULT_ERROR;
  jw_discovery_page_t v;
  memset(&v, 0, sizeof(v));
  v.generation = get_u32(data);
  v.total = get_u16(data + 4);
  v.next_cursor = get_u16(data + 6);
  v.count = data[8];
  for (uint8_t i = 0; i < v.count; ++i)
    if (jw_decode_function_descriptor(
            data + JW_DISCOVERY_HEADER_LEN + i * JW_FUNCTION_DESCRIPTOR_LEN,
            JW_FUNCTION_DESCRIPTOR_LEN,
            &v.functions[i]) != JW_RESULT_OK)
      return JW_RESULT_ERROR;
  if (!valid_page(&v)) return JW_RESULT_ERROR;
  *out = v;
  return JW_RESULT_OK;
}
static int valid_dependencies(const jw_dependency_page_t* v) {
  if (v->generation == 0 || v->count > JW_DEPENDENCY_PAGE_SIZE ||
      (v->count == 0 && v->next_cursor != JW_CURSOR_END))
    return 0;
  for (uint8_t i = 0; i < v->count; ++i)
    if (v->dependencies[i].function_id == JW_CHANNEL_NONE || v->dependencies[i].effects == 0)
      return 0;
  return 1;
}
int jw_encode_dependency_page(uint8_t* out, size_t cap, const jw_dependency_page_t* v) {
  if (out == NULL || v == NULL || !valid_dependencies(v)) return -1;
  const size_t len = JW_DEPENDENCY_HEADER_LEN + v->count * JW_DEPENDENCY_RECORD_LEN;
  if (cap < len) return -1;
  put_u32(out, v->generation);
  put_u16(out + 4, v->next_cursor);
  out[6] = v->count;
  for (uint8_t i = 0; i < v->count; ++i) {
    uint8_t* r = out + JW_DEPENDENCY_HEADER_LEN + i * JW_DEPENDENCY_RECORD_LEN;
    put_u16(r, v->dependencies[i].cause_domain);
    r[2] = v->dependencies[i].function_id;
    r[3] = v->dependencies[i].effects;
  }
  return (int)len;
}
jw_result_t jw_decode_dependency_page(const uint8_t* data, size_t len, jw_dependency_page_t* out) {
  if (data == NULL || out == NULL || len < JW_DEPENDENCY_HEADER_LEN ||
      data[6] > JW_DEPENDENCY_PAGE_SIZE ||
      len != JW_DEPENDENCY_HEADER_LEN + (size_t)data[6] * JW_DEPENDENCY_RECORD_LEN)
    return JW_RESULT_ERROR;
  jw_dependency_page_t v;
  memset(&v, 0, sizeof(v));
  v.generation = get_u32(data);
  v.next_cursor = get_u16(data + 4);
  v.count = data[6];
  for (uint8_t i = 0; i < v.count; ++i) {
    const uint8_t* r = data + JW_DEPENDENCY_HEADER_LEN + i * JW_DEPENDENCY_RECORD_LEN;
    v.dependencies[i].cause_domain = get_u16(r);
    v.dependencies[i].function_id = r[2];
    v.dependencies[i].effects = r[3];
  }
  if (!valid_dependencies(&v)) return JW_RESULT_ERROR;
  *out = v;
  return JW_RESULT_OK;
}
static int valid_state(const jw_device_status_t* v) {
  if (v->verification == JW_SAFE_VERIFIED && v->stop_phase != JW_STOP_ACTION_COMPLETE) return 0;
  if ((v->action_session_id == 0) != (v->action_message_id == 0)) return 0;
  if (v->action_message_id == 0) {
    if (v->action_status != JW_STATUS_INVALID || v->action_execution != JW_EXECUTION_REJECTED)
      return 0;
  } else if (!valid_reply(v->action_status, v->action_execution, 0))
    return 0;
  return v->state >= JW_STATE_UNKNOWN && v->state <= JW_STATE_FAULT &&
         v->reason >= JW_REASON_NONE && v->reason <= JW_REASON_REBOOT &&
         v->stop_phase >= JW_STOP_NONE && v->stop_phase <= JW_STOP_ACTION_COMPLETE &&
         v->verification >= JW_SAFE_UNSUPPORTED && v->verification <= JW_SAFE_NOT_VERIFIED;
}
int jw_encode_device_status(uint8_t* out, size_t cap, const jw_device_status_t* v) {
  if (out == NULL || v == NULL || cap < JW_DEVICE_STATUS_LEN || !valid_state(v)) return -1;
  out[0] = (uint8_t)v->state;
  out[1] = (uint8_t)v->reason;
  out[2] = v->recovery;
  out[3] = (uint8_t)v->stop_phase;
  out[4] = (uint8_t)v->verification;
  put_u32(out + 5, v->epoch);
  put_u32(out + 9, v->action_session_id);
  put_u32(out + 13, v->action_message_id);
  out[17] = (uint8_t)v->action_execution;
  out[18] = (uint8_t)v->action_status;
  return JW_DEVICE_STATUS_LEN;
}
jw_result_t jw_decode_device_status(const uint8_t* data, size_t len, jw_device_status_t* out) {
  if (data == NULL || out == NULL || len != JW_DEVICE_STATUS_LEN) return JW_RESULT_ERROR;
  const jw_device_status_t v = {(jw_device_state_t)data[0],
                                (jw_state_reason_t)data[1],
                                data[2],
                                (jw_stop_phase_t)data[3],
                                (jw_safe_verification_t)data[4],
                                get_u32(data + 5),
                                get_u32(data + 9),
                                get_u32(data + 13),
                                (jw_execution_t)data[17],
                                (jw_status_t)data[18]};
  if (!valid_state(&v)) return JW_RESULT_ERROR;
  *out = v;
  return JW_RESULT_OK;
}
int jw_encode_profile_request(uint8_t* out, size_t cap, const jw_profile_request_t* v) {
  if (out == NULL || v == NULL || v->profile_id == 0 || v->major == 0 || v->operation >= 32 ||
      v->data_len > JW_PAYLOAD_LIMIT - JW_PROFILE_HEADER_LEN ||
      cap < JW_PROFILE_HEADER_LEN + v->data_len || (v->data_len != 0 && v->data == NULL))
    return -1;
  if (v->data_len != 0) memmove(out + JW_PROFILE_HEADER_LEN, v->data, v->data_len);
  put_u16(out, v->profile_id);
  out[2] = v->major;
  out[3] = v->minor;
  out[4] = v->operation;
  return (int)(JW_PROFILE_HEADER_LEN + v->data_len);
}
jw_result_t jw_decode_profile_request(const uint8_t* data, size_t len, jw_profile_request_t* out) {
  if (data == NULL || out == NULL || len < JW_PROFILE_HEADER_LEN || len > JW_PAYLOAD_LIMIT ||
      get_u16(data) == 0 || data[2] == 0 || data[4] >= 32)
    return JW_RESULT_ERROR;
  const jw_profile_request_t v = {get_u16(data),
                                  data[2],
                                  data[3],
                                  data[4],
                                  data + JW_PROFILE_HEADER_LEN,
                                  len - JW_PROFILE_HEADER_LEN};
  *out = v;
  return JW_RESULT_OK;
}
int jw_function_supports_operation(const jw_function_descriptor_t* f,
                                   const jw_profile_request_t* r) {
  return f != NULL && r != NULL && valid_function(f) && f->profile_id == r->profile_id &&
         f->profile_major == r->major && r->minor <= f->profile_minor && r->operation < 32 &&
         (f->operations & (UINT32_C(1) << r->operation)) != 0;
}
