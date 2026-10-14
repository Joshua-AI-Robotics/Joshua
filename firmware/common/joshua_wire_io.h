// I/O-specific JoshuaWire value interfaces. The advertised interface selects
// the payload layout; these semantic structs are never copied onto the wire.
#pragma once

#include <stdint.h>

typedef enum {
  JW_IO_INTERFACE_INVALID = 0,
  JW_IO_INTERFACE_DIGITAL = 1,
  JW_IO_INTERFACE_ANALOG_F32 = 2,
} jw_io_interface_t;

#define JW_IO_DIGITAL_PAYLOAD_LEN 1
#define JW_IO_ANALOG_PAYLOAD_LEN 4

typedef struct {
  jw_io_interface_t interface;
  union {
    uint8_t digital;  // Exactly 0 or 1.
    float analog;     // Native channel units.
  } value;
} jw_io_value_t;
