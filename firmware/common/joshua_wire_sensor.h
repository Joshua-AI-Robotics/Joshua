// Sensor-specific JoshuaWire sample interfaces. Values use native channel units.
// Add interface IDs and their configuration/sample codecs together; never reuse IDs.
#pragma once

#include <stdint.h>

typedef enum {
  JW_SENSOR_INTERFACE_INVALID = 0,
  JW_SENSOR_INTERFACE_SCALAR_F32 = 1,
} jw_sensor_interface_t;

#define JW_SENSOR_SCALAR_SAMPLE_PAYLOAD_LEN 4

typedef struct {
  float value;
} jw_sensor_sample_t;
