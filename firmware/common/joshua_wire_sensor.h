// Sensor profile v1.0: typed scalar measurements with explicit freshness.
#pragma once
#include <stdint.h>

typedef enum {
  JW_SENSOR_INTERFACE_INVALID = 0,
  JW_SENSOR_INTERFACE_SCALAR_F32 = 1
} jw_sensor_interface_t;
typedef enum {
  JW_UNIT_DIMENSIONLESS = 0,
  JW_UNIT_RADIAN = 1,
  JW_UNIT_RADIAN_PER_SECOND = 2,
  JW_UNIT_CELSIUS = 3,
  JW_UNIT_VOLT = 4,
  JW_UNIT_AMPERE = 5,
  JW_UNIT_METRE = 6,
  JW_UNIT_NATIVE = 0xffff,  // Vendor units must be documented by the profile.
} jw_unit_t;
typedef enum {
  JW_CLOCK_UNSPECIFIED = 0,
  JW_CLOCK_BOOT_MONOTONIC_US = 1,
  JW_CLOCK_SYNC_US = 2
} jw_clock_t;
typedef enum {
  JW_SAMPLE_VALID = 1,
  JW_SAMPLE_STALE = 2,
  JW_SAMPLE_INVALID = 3
} jw_sample_validity_t;
#define JW_MEASUREMENT_METADATA_LEN 18
#define JW_SENSOR_SCHEMA_LEN 8
#define JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN (JW_MEASUREMENT_METADATA_LEN + 4)
typedef struct {
  jw_sensor_interface_t data_type;
  uint16_t unit;
  jw_clock_t clock;
  uint32_t max_age_us;
} jw_sensor_schema_t;
typedef struct {
  jw_sample_validity_t validity;
  jw_clock_t clock;
  uint32_t boot_id;
  uint64_t timestamp_us;  // Acquisition time, not request/response time.
  uint32_t sequence;      // Acquisition sequence, unchanged on cached reads/retries.
} jw_measurement_metadata_t;
typedef struct {
  float value;
  jw_measurement_metadata_t metadata;
} jw_sensor_sample_t;
