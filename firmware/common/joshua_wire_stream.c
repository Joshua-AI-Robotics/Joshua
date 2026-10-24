#include "joshua_wire_stream.h"

#include <string.h>

void jw_stream_init(jw_stream_decoder_t* decoder) {
  memset(decoder, 0, sizeof(*decoder));
}
int jw_stream_feed(jw_stream_decoder_t* d, uint8_t byte, uint32_t now_ms) {
  if (d->used && (uint32_t)(now_ms - d->last_byte_ms) >= 20u) d->used = 0;
  d->last_byte_ms = now_ms;
  if (d->used == 0) {
    if (byte == JW_SYNC_BYTE) d->bytes[d->used++] = byte;
    return 0;
  }
  d->bytes[d->used++] = byte;
  if (d->used == JW_LENGTH_PREFIX_LEN) {
    d->expected = (size_t)byte + JW_LENGTH_FIELD_OVERHEAD;
    if (d->expected < JW_MIN_FRAME_LEN || d->expected > JW_MAX_FRAME_LEN) {
      d->used = 0;
      return -1;
    }
  }
  if (d->used < d->expected) return 0;
  const size_t size = d->used;
  d->used = 0;
  jw_frame_t frame;
  return jw_decode_frame(d->bytes, size, &frame) == JW_RESULT_OK ? (int)size : -1;
}
