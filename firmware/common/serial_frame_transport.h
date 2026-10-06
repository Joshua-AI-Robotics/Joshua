// Shared serial link adapter. Board bindings supply bounded, nonblocking I/O.
#pragma once

#include "serial_frame_assembler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void* context;
  // Returns one byte (0..255), or -1 when none is immediately available.
  int (*read_byte)(void* context);
  // Consumes/copies up to length bytes, returning the accepted byte count.
  // Returns zero when blocked. Must never wait for space or remote reception.
  size_t (*write_bytes)(void* context, const uint8_t* bytes, size_t length);
  uint32_t (*now_ms)(void* context);
  uint32_t byte_timeout_ms;
} serial_frame_transport_config_t;

typedef struct {
  serial_frame_transport_config_t config;
  serial_frame_assembler_t assembler;
  uint8_t transmit[JW_MAX_FRAME_LEN];
  size_t transmit_length;
  size_t transmit_offset;
} serial_frame_transport_t;

// Copies config; state and callback context must outlive the returned interface.
// Accepted frames are copied into one adapter-owned TX slot; polls/send attempts
// advance outstanding bytes. While that slot is occupied, try_send accepts none
// of the next frame and returns FRAME_WOULD_BLOCK. RX handles at most 64 bytes
// per call. No codec/session/command handling happens in this adapter.
frame_transport_t serial_frame_transport_init(serial_frame_transport_t* state,
                                              const serial_frame_transport_config_t* config);

#ifdef __cplusplus
}
#endif
