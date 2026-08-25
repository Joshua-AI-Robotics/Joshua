// UART/USB-serial transport for joshua_wire_v1 frames
// (docs/BOARD_LAYER_RFC.md §7.3 — the [JOSHUA_TRANSPORT_SERIAL] variant of
// the transport seam). A future UDP/Wi-Fi variant implements the same two
// functions over a different physical link with zero changes to
// main.cpp's dispatch loop.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "joshua_wire_v1.h"

#ifdef __cplusplus
extern "C" {
#endif

void TransportInit(void);

// Blocks up to a short timeout waiting for sync, then reads a complete
// frame (header first to learn `len`, then the rest) and CRC-validates it
// via jw1_decode_frame. Returns true and fills `out` on a good frame;
// false on timeout or a framing/CRC error (the caller simply loops back —
// there is no NAK in this protocol version, the host's own request will
// eventually time out and it may retry).
bool TransportReadFrame(jw1_frame_t* out, uint8_t* frame_buf, size_t frame_buf_cap);

void TransportWriteFrame(const uint8_t* frame, size_t len);

#ifdef __cplusplus
}
#endif
