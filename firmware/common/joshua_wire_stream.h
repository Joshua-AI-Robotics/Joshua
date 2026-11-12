// Incremental serial framing. One decoder belongs to one receiver, no blocking
// reads or protocol-version probing. Completed storage is borrowed until Feed.
#pragma once

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
} jw_stream_decoder_t;

void jw_stream_init(jw_stream_decoder_t* decoder);
void jw_stream_init_timeout(jw_stream_decoder_t* decoder, uint32_t byte_timeout_ms);
// Returns complete frame size, 0 for partial input, or -1 for invalid input.
// A partial frame expires after the configured timeout (20 ms by default).
int jw_stream_feed(jw_stream_decoder_t* decoder, uint8_t byte, uint32_t now_ms);
#ifdef __cplusplus
}
#endif
