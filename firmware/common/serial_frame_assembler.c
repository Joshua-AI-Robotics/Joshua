#include "serial_frame_assembler.h"

#include <string.h>
void serial_frame_assembler_init(serial_frame_assembler_t* assembler, uint32_t byte_timeout_ms) {
  jw_stream_init_timeout(assembler, byte_timeout_ms);
}
frame_status_t serial_frame_assembler_push(serial_frame_assembler_t* assembler,
                                           uint32_t now_ms,
                                           uint8_t byte,
                                           uint8_t* frame,
                                           size_t capacity,
                                           size_t* length) {
  if (!length) return FRAME_ERROR;
  *length = 0;
  if (!assembler || !frame || capacity < JW_MAX_FRAME_LEN) return FRAME_ERROR;
  const int size = jw_stream_feed(assembler, byte, now_ms);
  if (size < 0) return FRAME_ERROR;
  if (!size) return FRAME_NO_DATA;
  memcpy(frame, assembler->bytes, size);
  *length = size;
  return FRAME_OK;
}
