#include "serial_frame_assembler.h"

#include <string.h>

void serial_frame_assembler_init(serial_frame_assembler_t* assembler, uint32_t byte_timeout_ms) {
  memset(assembler, 0, sizeof(*assembler));
  assembler->byte_timeout_ms = byte_timeout_ms;
}

frame_status_t serial_frame_assembler_push(serial_frame_assembler_t* assembler,
                                           uint32_t now_ms,
                                           uint8_t byte,
                                           uint8_t* frame,
                                           size_t capacity,
                                           size_t* length) {
  if (length == NULL) return FRAME_ERROR;
  *length = 0;
  if (assembler == NULL || frame == NULL || capacity < JW_MAX_FRAME_LEN) return FRAME_ERROR;
  if (assembler->used != 0 && now_ms - assembler->last_byte_ms >= assembler->byte_timeout_ms) {
    assembler->used = 0;
  }
  if (assembler->used == 0 && byte != JW_SYNC_BYTE) return FRAME_NO_DATA;
  assembler->last_byte_ms = now_ms;
  assembler->bytes[assembler->used++] = byte;
  if (assembler->used == 2) {
    // Include short legacy frames so the shared codec rejects their version.
    assembler->expected = (size_t)byte + 4;
    if (byte < 3 || assembler->expected > JW_MAX_FRAME_LEN) {
      assembler->used = 0;
      return FRAME_ERROR;
    }
  }
  if (assembler->used < 2 || assembler->used != assembler->expected) return FRAME_NO_DATA;
  memcpy(frame, assembler->bytes, assembler->expected);
  *length = assembler->expected;
  assembler->used = 0;
  return FRAME_OK;
}
