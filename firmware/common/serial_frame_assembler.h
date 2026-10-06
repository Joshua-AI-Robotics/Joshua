// Incremental JW serial link framing; CRC/session/command checks stay in JW.
#pragma once

#include "frame_transport.h"
#include "joshua_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint8_t bytes[JW_MAX_FRAME_LEN];
  size_t used;
  size_t expected;
  uint32_t last_byte_ms;
  uint32_t byte_timeout_ms;
} serial_frame_assembler_t;

// The adapter supplies a monotonic, wrapping millisecond clock and an explicit
// inter-byte timeout. A stalled partial frame is discarded on the next push.
void serial_frame_assembler_init(serial_frame_assembler_t* assembler, uint32_t byte_timeout_ms);
// Consumes one byte, copying a completed frame into caller storage. FRAME_ERROR
// discards invalid length/capacity input; FRAME_NO_DATA retains partial input.
frame_status_t serial_frame_assembler_push(serial_frame_assembler_t* assembler,
                                           uint32_t now_ms,
                                           uint8_t byte,
                                           uint8_t* frame,
                                           size_t capacity,
                                           size_t* length);

#ifdef __cplusplus
}
#endif
