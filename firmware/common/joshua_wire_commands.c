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
static int channel_interface_id(const jw_channel_descriptor_t* channel) {
  switch (channel->kind) {
    case JW_CHANNEL_KIND_ACTUATOR:
      return (int)channel->interface.actuator_drive;
    case JW_CHANNEL_KIND_SENSOR:
      return (int)channel->interface.sensor_interface;
    case JW_CHANNEL_KIND_IO:
      return (int)channel->interface.io_interface;
    default:
      return 0;
  }
}

static int valid_channel(const jw_channel_descriptor_t* channel) {
  if (channel->channel_id == JW_CHANNEL_NONE) return 0;
  const int interface = channel_interface_id(channel);
  switch (channel->kind) {
    case JW_CHANNEL_KIND_ACTUATOR:
      return interface >= JW_DRIVE_STEP_DIR && interface <= JW_DRIVE_PDO_JOINT;
    case JW_CHANNEL_KIND_SENSOR:
      return interface == JW_SENSOR_INTERFACE_SCALAR_F32;
    case JW_CHANNEL_KIND_IO:
      return interface == JW_IO_INTERFACE_DIGITAL || interface == JW_IO_INTERFACE_ANALOG_F32;
    default:
      return 0;
  }
}

size_t jw_identify_payload_len(uint8_t n_channels) {
  if (n_channels > JW_MAX_CHANNELS) return 0;
  return JW_IDENTIFY_MIN_PAYLOAD_LEN + (size_t)n_channels * JW_CHANNEL_DESCRIPTOR_WIRE_LEN;
}

int jw_encode_identify_payload(uint8_t* out, size_t cap, const jw_identify_response_t* value) {
  if (out == NULL || value == NULL) return -1;
  const size_t len = jw_identify_payload_len(value->n_channels);
  if (len == 0 || cap < len || (unsigned)value->board_id > UINT8_MAX) return -1;
  // Validate every descriptor before modifying the caller's output buffer.
  for (uint8_t i = 0; i < value->n_channels; ++i) {
    if (!valid_channel(&value->channels[i])) return -1;
    for (uint8_t j = 0; j < i; ++j) {
      if (value->channels[i].channel_id == value->channels[j].channel_id) return -1;
    }
  }
  out[JW_IDENTIFY_BOARD_OFFSET] = (uint8_t)value->board_id;
  memcpy(out + JW_IDENTIFY_NAME_OFFSET, value->fw_name, JW_FW_NAME_LEN);
  out[JW_IDENTIFY_COUNT_OFFSET] = value->n_channels;
  for (uint8_t i = 0; i < value->n_channels; ++i) {
    const jw_channel_descriptor_t* channel = &value->channels[i];
    uint8_t* wire = out + JW_IDENTIFY_CHANNELS_OFFSET + i * JW_CHANNEL_DESCRIPTOR_WIRE_LEN;
    wire[JW_CHANNEL_ID_OFFSET] = channel->channel_id;
    wire[JW_CHANNEL_KIND_OFFSET] = (uint8_t)channel->kind;
    put_u32(wire + JW_CHANNEL_CAPABILITIES_OFFSET, channel->capabilities);
    wire[JW_CHANNEL_INTERFACE_OFFSET] = (uint8_t)channel_interface_id(channel);
  }
  return (int)len;
}

jw_result_t jw_decode_identify_payload(const uint8_t* data,
                                       size_t len,
                                       jw_identify_response_t* out) {
  if (data == NULL || out == NULL || len < JW_IDENTIFY_MIN_PAYLOAD_LEN) return JW_RESULT_ERROR;
  const uint8_t count = data[JW_IDENTIFY_COUNT_OFFSET];
  const size_t expected_len = jw_identify_payload_len(count);
  if (expected_len == 0 || len != expected_len) return JW_RESULT_ERROR;
  jw_identify_response_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  decoded.board_id = (jw_board_id_t)data[JW_IDENTIFY_BOARD_OFFSET];
  memcpy(decoded.fw_name, data + JW_IDENTIFY_NAME_OFFSET, JW_FW_NAME_LEN);
  decoded.n_channels = count;
  for (uint8_t i = 0; i < count; ++i) {
    const uint8_t* wire = data + JW_IDENTIFY_CHANNELS_OFFSET + i * JW_CHANNEL_DESCRIPTOR_WIRE_LEN;
    jw_channel_descriptor_t* channel = &decoded.channels[i];
    channel->channel_id = wire[JW_CHANNEL_ID_OFFSET];
    const uint8_t kind = wire[JW_CHANNEL_KIND_OFFSET];
    if (kind < JW_CHANNEL_KIND_ACTUATOR || kind > JW_CHANNEL_KIND_IO) return JW_RESULT_ERROR;
    channel->kind = (jw_channel_kind_t)kind;
    channel->capabilities = get_u32(wire + JW_CHANNEL_CAPABILITIES_OFFSET);
    const uint8_t interface = wire[JW_CHANNEL_INTERFACE_OFFSET];
    switch (channel->kind) {
      case JW_CHANNEL_KIND_ACTUATOR:
        if (interface < JW_DRIVE_STEP_DIR || interface > JW_DRIVE_PDO_JOINT) return JW_RESULT_ERROR;
        channel->interface.actuator_drive = (jw_drive_t)interface;
        break;
      case JW_CHANNEL_KIND_SENSOR:
        if (interface != JW_SENSOR_INTERFACE_SCALAR_F32) return JW_RESULT_ERROR;
        channel->interface.sensor_interface = (jw_sensor_interface_t)interface;
        break;
      case JW_CHANNEL_KIND_IO:
        if (interface != JW_IO_INTERFACE_DIGITAL && interface != JW_IO_INTERFACE_ANALOG_F32)
          return JW_RESULT_ERROR;
        channel->interface.io_interface = (jw_io_interface_t)interface;
        break;
      default:
        return JW_RESULT_ERROR;
    }
    if (!valid_channel(channel)) return JW_RESULT_ERROR;
    for (uint8_t j = 0; j < i; ++j) {
      if (channel->channel_id == decoded.channels[j].channel_id) return JW_RESULT_ERROR;
    }
  }
  *out = decoded;
  return JW_RESULT_OK;
}

int jw_channel_supports_command(const jw_channel_descriptor_t* channel, uint8_t cmd) {
  if (channel == NULL || !valid_channel(channel)) return 0;
  const int interface = channel_interface_id(channel);
  uint32_t capability;
  switch (cmd) {
    case JW_CMD_CONFIGURE_CHANNEL:
      // Other interfaces need their own configuration schemas before dispatch.
      if (channel->kind != JW_CHANNEL_KIND_ACTUATOR || interface != JW_DRIVE_STEP_DIR) return 0;
      capability = JW_CAP_CONFIGURE;
      break;
    case JW_CMD_GET_FEEDBACK:
      if (channel->kind != JW_CHANNEL_KIND_ACTUATOR) return 0;
      capability = JW_CAP_READ;
      break;
    case JW_CMD_SET_TARGET:
      if (channel->kind != JW_CHANNEL_KIND_ACTUATOR) return 0;
      capability = JW_CAP_WRITE;
      break;
    case JW_CMD_READ_CHANNEL:
      if (channel->kind != JW_CHANNEL_KIND_SENSOR && channel->kind != JW_CHANNEL_KIND_IO) return 0;
      capability = JW_CAP_READ;
      break;
    case JW_CMD_WRITE_CHANNEL:
      if (channel->kind != JW_CHANNEL_KIND_IO) return 0;
      capability = JW_CAP_WRITE;
      break;
    case JW_CMD_ENABLE:
    case JW_CMD_DISABLE:
      capability = JW_CAP_ENABLE_DISABLE;
      break;
    default:
      return 0;
  }
  return (channel->capabilities & capability) != 0;
}

int jw_encode_sensor_sample_payload(uint8_t* out,
                                    size_t cap,
                                    jw_sensor_interface_t interface,
                                    const jw_sensor_sample_t* value) {
  if (out == NULL || value == NULL || interface != JW_SENSOR_INTERFACE_SCALAR_F32 ||
      cap < JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN)
    return -1;
  put_float(out, value->value);
  return JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN;
}

jw_result_t jw_decode_sensor_sample_payload(const uint8_t* data,
                                            size_t len,
                                            jw_sensor_interface_t interface,
                                            jw_sensor_sample_t* out) {
  if (data == NULL || out == NULL || interface != JW_SENSOR_INTERFACE_SCALAR_F32 ||
      len != JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN)
    return JW_RESULT_ERROR;
  out->value = get_float(data);
  return JW_RESULT_OK;
}

int jw_encode_io_payload(uint8_t* out, size_t cap, const jw_io_value_t* value) {
  if (out == NULL || value == NULL) return -1;
  switch (value->interface) {
    case JW_IO_INTERFACE_DIGITAL:
      if (cap < JW_IO_DIGITAL_PAYLOAD_LEN || value->value.digital > 1) return -1;
      out[0] = value->value.digital;
      return JW_IO_DIGITAL_PAYLOAD_LEN;
    case JW_IO_INTERFACE_ANALOG_F32:
      if (cap < JW_IO_ANALOG_PAYLOAD_LEN) return -1;
      put_float(out, value->value.analog);
      return JW_IO_ANALOG_PAYLOAD_LEN;
    default:
      return -1;
  }
}

jw_result_t jw_decode_io_payload(const uint8_t* data,
                                 size_t len,
                                 jw_io_interface_t interface,
                                 jw_io_value_t* out) {
  if (data == NULL || out == NULL) return JW_RESULT_ERROR;
  jw_io_value_t decoded;
  memset(&decoded, 0, sizeof(decoded));
  decoded.interface = interface;
  switch (interface) {
    case JW_IO_INTERFACE_DIGITAL:
      if (len != JW_IO_DIGITAL_PAYLOAD_LEN || data[0] > 1) return JW_RESULT_ERROR;
      decoded.value.digital = data[0];
      break;
    case JW_IO_INTERFACE_ANALOG_F32:
      if (len != JW_IO_ANALOG_PAYLOAD_LEN) return JW_RESULT_ERROR;
      decoded.value.analog = get_float(data);
      break;
    default:
      return JW_RESULT_ERROR;
  }
  *out = decoded;
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
  if (out == NULL || cap < JW_SET_TARGET_PAYLOAD_LEN || mode == JW_MODE_COMPLEX) return -1;
  out[0] = (uint8_t)mode;
  put_float(out + 1, value);
  return JW_SET_TARGET_PAYLOAD_LEN;
}
int jw_encode_complex_target_payload(uint8_t* out, size_t cap, const jw_complex_target_t* value) {
  if (out == NULL || value == NULL || cap < JW_SET_TARGET_COMPLEX_PAYLOAD_LEN) return -1;
  out[0] = JW_MODE_COMPLEX;
  uint8_t* field = out + 1;
  put_float(field, value->position);
  field += JW_TARGET_FLOAT_WIRE_LEN;
  put_float(field, value->velocity);
  field += JW_TARGET_FLOAT_WIRE_LEN;
  put_float(field, value->torque);
  return JW_SET_TARGET_COMPLEX_PAYLOAD_LEN;
}
jw_result_t jw_decode_set_target_payload(const uint8_t* data, size_t len, jw_set_target_t* out) {
  if (data == NULL || out == NULL || len == 0) return JW_RESULT_ERROR;
  const jw_mode_t mode = (jw_mode_t)data[0];
  const size_t expected_len =
      mode == JW_MODE_COMPLEX ? JW_SET_TARGET_COMPLEX_PAYLOAD_LEN : JW_SET_TARGET_PAYLOAD_LEN;
  if (len != expected_len) return JW_RESULT_ERROR;
  jw_set_target_t decoded = {mode, 0.0f, {0.0f, 0.0f, 0.0f}};
  if (mode == JW_MODE_COMPLEX) {
    const uint8_t* field = data + 1;
    decoded.combined.position = get_float(field);
    field += JW_TARGET_FLOAT_WIRE_LEN;
    decoded.combined.velocity = get_float(field);
    field += JW_TARGET_FLOAT_WIRE_LEN;
    decoded.combined.torque = get_float(field);
  } else {
    decoded.value = get_float(data + 1);
  }
  *out = decoded;
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
