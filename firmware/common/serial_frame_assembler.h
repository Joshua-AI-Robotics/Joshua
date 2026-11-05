// Caller-storage adapter for the shared JoshuaWire stream decoder.
#pragma once

#include "frame_transport.h"
#include "joshua_wire_stream.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef jw_stream_decoder_t serial_frame_assembler_t;

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
