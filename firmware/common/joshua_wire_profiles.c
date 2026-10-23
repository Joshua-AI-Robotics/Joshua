// Versioned device-profile payload codecs. No framing, sessions or hardware I/O.
#include "joshua_wire_profiles.h"

#include <math.h>
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

static int valid_metadata(const jw_measurement_metadata_t* v) {
  return v->validity >= JW_SAMPLE_VALID && v->validity <= JW_SAMPLE_INVALID &&
         v->clock >= JW_CLOCK_BOOT_MONOTONIC_US && v->clock <= JW_CLOCK_SYNC_US && v->boot_id != 0;
}
int jw_encode_measurement_metadata(uint8_t* out, size_t cap, const jw_measurement_metadata_t* v) {
  if (out == NULL || v == NULL || cap < JW_MEASUREMENT_METADATA_LEN || !valid_metadata(v))
    return -1;
  out[0] = (uint8_t)v->validity;
  out[1] = (uint8_t)v->clock;
  put_u32(out + 2, v->boot_id);
  put_u32(out + 6, (uint32_t)v->timestamp_us);
  put_u32(out + 10, (uint32_t)(v->timestamp_us >> 32));
  put_u32(out + 14, v->sequence);
  return JW_MEASUREMENT_METADATA_LEN;
}
jw_result_t jw_decode_measurement_metadata(const uint8_t* data,
                                           size_t len,
                                           jw_measurement_metadata_t* out) {
  if (data == NULL || out == NULL || len != JW_MEASUREMENT_METADATA_LEN) return JW_RESULT_ERROR;
  const jw_measurement_metadata_t v = {
      (jw_sample_validity_t)data[0],
      (jw_clock_t)data[1],
      get_u32(data + 2),
      (uint64_t)get_u32(data + 6) | ((uint64_t)get_u32(data + 10) << 32),
      get_u32(data + 14)};
  if (!valid_metadata(&v)) return JW_RESULT_ERROR;
  *out = v;
  return JW_RESULT_OK;
}
int jw_encode_sensor_schema(uint8_t* out, size_t cap, const jw_sensor_schema_t* v) {
  if (out == NULL || v == NULL || cap < JW_SENSOR_SCHEMA_LEN ||
      v->data_type != JW_SENSOR_INTERFACE_SCALAR_F32 || v->clock < JW_CLOCK_BOOT_MONOTONIC_US ||
      v->clock > JW_CLOCK_SYNC_US || v->max_age_us == 0)
    return -1;
  out[0] = (uint8_t)v->data_type;
  put_u16(out + 1, v->unit);
  out[3] = (uint8_t)v->clock;
  put_u32(out + 4, v->max_age_us);
  return JW_SENSOR_SCHEMA_LEN;
}
jw_result_t jw_decode_sensor_schema(const uint8_t* data, size_t len, jw_sensor_schema_t* out) {
  if (data == NULL || out == NULL || len != JW_SENSOR_SCHEMA_LEN ||
      data[0] != JW_SENSOR_INTERFACE_SCALAR_F32 || data[3] < JW_CLOCK_BOOT_MONOTONIC_US ||
      data[3] > JW_CLOCK_SYNC_US || get_u32(data + 4) == 0)
    return JW_RESULT_ERROR;
  const jw_sensor_schema_t v = {
      (jw_sensor_interface_t)data[0], get_u16(data + 1), (jw_clock_t)data[3], get_u32(data + 4)};
  *out = v;
  return JW_RESULT_OK;
}
int jw_encode_sensor_sample_payload(uint8_t* out,
                                    size_t cap,
                                    jw_sensor_interface_t interface,
                                    const jw_sensor_sample_t* v) {
  if (out == NULL || v == NULL || interface != JW_SENSOR_INTERFACE_SCALAR_F32 ||
      cap < JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN || !valid_metadata(&v->metadata) ||
      (v->metadata.validity != JW_SAMPLE_INVALID && !isfinite(v->value)))
    return -1;
  jw_encode_measurement_metadata(out, cap, &v->metadata);
  put_float(out + JW_MEASUREMENT_METADATA_LEN, v->value);
  return JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN;
}
jw_result_t jw_decode_sensor_sample_payload(const uint8_t* data,
                                            size_t len,
                                            jw_sensor_interface_t interface,
                                            jw_sensor_sample_t* out) {
  if (data == NULL || out == NULL || interface != JW_SENSOR_INTERFACE_SCALAR_F32 ||
      len != JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN)
    return JW_RESULT_ERROR;
  jw_sensor_sample_t v;
  if (jw_decode_measurement_metadata(data, JW_MEASUREMENT_METADATA_LEN, &v.metadata) !=
      JW_RESULT_OK)
    return JW_RESULT_ERROR;
  v.value = get_float(data + JW_MEASUREMENT_METADATA_LEN);
  if (v.metadata.validity != JW_SAMPLE_INVALID && !isfinite(v.value)) return JW_RESULT_ERROR;
  *out = v;
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
int jw_encode_actuator_schema(uint8_t* out, size_t cap, const jw_actuator_schema_t* v) {
  if (out == NULL || v == NULL || cap < JW_ACTUATOR_SCHEMA_LEN || v->drive < JW_DRIVE_STEP_DIR ||
      v->drive > JW_DRIVE_PDO_JOINT || (v->target_modes & 1u) != 0 ||
      v->feedback_clock < JW_CLOCK_BOOT_MONOTONIC_US || v->feedback_clock > JW_CLOCK_SYNC_US ||
      v->max_feedback_age_us == 0)
    return -1;
  out[0] = (uint8_t)v->drive;
  put_u32(out + 1, v->target_modes);
  put_u16(out + 5, v->position_unit);
  put_u16(out + 7, v->velocity_unit);
  put_u16(out + 9, v->effort_unit);
  out[11] = (uint8_t)v->feedback_clock;
  put_u32(out + 12, v->max_feedback_age_us);
  return JW_ACTUATOR_SCHEMA_LEN;
}
jw_result_t jw_decode_actuator_schema(const uint8_t* data, size_t len, jw_actuator_schema_t* out) {
  if (data == NULL || out == NULL || len != JW_ACTUATOR_SCHEMA_LEN || data[0] < JW_DRIVE_STEP_DIR ||
      data[0] > JW_DRIVE_PDO_JOINT || (get_u32(data + 1) & 1u) != 0 ||
      data[11] < JW_CLOCK_BOOT_MONOTONIC_US || data[11] > JW_CLOCK_SYNC_US ||
      get_u32(data + 12) == 0)
    return JW_RESULT_ERROR;
  const jw_actuator_schema_t v = {(jw_drive_t)data[0],
                                  get_u32(data + 1),
                                  get_u16(data + 5),
                                  get_u16(data + 7),
                                  get_u16(data + 9),
                                  (jw_clock_t)data[11],
                                  get_u32(data + 12)};
  *out = v;
  return JW_RESULT_OK;
}
int jw_encode_feedback_payload(uint8_t* out, size_t cap, const jw_feedback_t* v) {
  if (out == NULL || v == NULL || cap < JW_FEEDBACK_RESPONSE_PAYLOAD_LEN ||
      !valid_metadata(&v->metadata) ||
      (v->metadata.validity != JW_SAMPLE_INVALID &&
       (!isfinite(v->position) || !isfinite(v->velocity))))
    return -1;
  jw_encode_measurement_metadata(out, cap, &v->metadata);
  put_float(out + JW_MEASUREMENT_METADATA_LEN, v->position);
  put_float(out + JW_MEASUREMENT_METADATA_LEN + 4, v->velocity);
  put_u16(out + JW_MEASUREMENT_METADATA_LEN + 8, v->fault_flags);
  return JW_FEEDBACK_RESPONSE_PAYLOAD_LEN;
}
jw_result_t jw_decode_feedback_payload(const uint8_t* data, size_t len, jw_feedback_t* out) {
  if (data == NULL || out == NULL || len != JW_FEEDBACK_RESPONSE_PAYLOAD_LEN)
    return JW_RESULT_ERROR;
  jw_feedback_t v;
  if (jw_decode_measurement_metadata(data, JW_MEASUREMENT_METADATA_LEN, &v.metadata) !=
      JW_RESULT_OK)
    return JW_RESULT_ERROR;
  v.position = get_float(data + JW_MEASUREMENT_METADATA_LEN);
  v.velocity = get_float(data + JW_MEASUREMENT_METADATA_LEN + 4);
  v.fault_flags = get_u16(data + JW_MEASUREMENT_METADATA_LEN + 8);
  if (v.metadata.validity != JW_SAMPLE_INVALID && (!isfinite(v.position) || !isfinite(v.velocity)))
    return JW_RESULT_ERROR;
  *out = v;
  return JW_RESULT_OK;
}
int jw_encode_set_target_payload(uint8_t* out, size_t cap, jw_mode_t mode, float value) {
  if (out == NULL || cap < JW_SET_TARGET_PAYLOAD_LEN ||
      (mode < JW_MODE_POSITION || mode > JW_MODE_TORQUE) || !isfinite(value))
    return -1;
  out[0] = (uint8_t)mode;
  put_float(out + 1, value);
  return JW_SET_TARGET_PAYLOAD_LEN;
}
int jw_encode_complex_target_payload(uint8_t* out, size_t cap, const jw_complex_target_t* value) {
  if (out == NULL || value == NULL || cap < JW_SET_TARGET_COMPLEX_PAYLOAD_LEN ||
      !isfinite(value->position) || !isfinite(value->velocity) || !isfinite(value->torque))
    return -1;
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
  if (mode < JW_MODE_POSITION || mode > JW_MODE_COMPLEX || len != expected_len)
    return JW_RESULT_ERROR;
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
  if (!isfinite(decoded.value) || !isfinite(decoded.combined.position) ||
      !isfinite(decoded.combined.velocity) || !isfinite(decoded.combined.torque))
    return JW_RESULT_ERROR;
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
  if (data == NULL || out == NULL || len != JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN || data[4] > 1 ||
      data[5] > 1)
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

static int valid_io_schema(const jw_io_schema_t* v) {
  return (v->interface == JW_IO_INTERFACE_DIGITAL || v->interface == JW_IO_INTERFACE_ANALOG_F32) &&
         (v->interface != JW_IO_INTERFACE_DIGITAL || v->unit == JW_UNIT_DIMENSIONLESS) &&
         v->clock >= JW_CLOCK_BOOT_MONOTONIC_US && v->clock <= JW_CLOCK_SYNC_US &&
         v->max_age_us != 0;
}
int jw_encode_io_schema(uint8_t* out, size_t cap, const jw_io_schema_t* v) {
  if (out == NULL || v == NULL || cap < JW_IO_SCHEMA_LEN || !valid_io_schema(v)) return -1;
  out[0] = (uint8_t)v->interface;
  put_u16(out + 1, v->unit);
  out[3] = (uint8_t)v->clock;
  put_u32(out + 4, v->max_age_us);
  return JW_IO_SCHEMA_LEN;
}
jw_result_t jw_decode_io_schema(const uint8_t* data, size_t len, jw_io_schema_t* out) {
  if (data == NULL || out == NULL || len != JW_IO_SCHEMA_LEN) return JW_RESULT_ERROR;
  const jw_io_schema_t v = {
      (jw_io_interface_t)data[0], get_u16(data + 1), (jw_clock_t)data[3], get_u32(data + 4)};
  if (!valid_io_schema(&v)) return JW_RESULT_ERROR;
  *out = v;
  return JW_RESULT_OK;
}
int jw_encode_io_sample(uint8_t* out, size_t cap, const jw_io_sample_t* v) {
  if (out == NULL || v == NULL || !valid_metadata(&v->metadata) ||
      (v->value.interface == JW_IO_INTERFACE_ANALOG_F32 &&
       v->metadata.validity != JW_SAMPLE_INVALID && !isfinite(v->value.value.analog)))
    return -1;
  uint8_t value[JW_IO_ANALOG_PAYLOAD_LEN];
  const int n = jw_encode_io_payload(value, sizeof(value), &v->value);
  if (n < 0 || cap < JW_MEASUREMENT_METADATA_LEN + (size_t)n) return -1;
  jw_encode_measurement_metadata(out, cap, &v->metadata);
  memcpy(out + JW_MEASUREMENT_METADATA_LEN, value, (size_t)n);
  return JW_MEASUREMENT_METADATA_LEN + n;
}
jw_result_t jw_decode_io_sample(const uint8_t* data,
                                size_t len,
                                jw_io_interface_t interface,
                                jw_io_sample_t* out) {
  if (data == NULL || out == NULL || len < JW_MEASUREMENT_METADATA_LEN) return JW_RESULT_ERROR;
  jw_io_sample_t v;
  if (jw_decode_measurement_metadata(data, JW_MEASUREMENT_METADATA_LEN, &v.metadata) !=
          JW_RESULT_OK ||
      jw_decode_io_payload(data + JW_MEASUREMENT_METADATA_LEN,
                           len - JW_MEASUREMENT_METADATA_LEN,
                           interface,
                           &v.value) != JW_RESULT_OK)
    return JW_RESULT_ERROR;
  if (v.value.interface == JW_IO_INTERFACE_ANALOG_F32 && v.metadata.validity != JW_SAMPLE_INVALID &&
      !isfinite(v.value.value.analog))
    return JW_RESULT_ERROR;
  *out = v;
  return JW_RESULT_OK;
}
