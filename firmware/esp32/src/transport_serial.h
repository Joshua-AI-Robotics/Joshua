// UART/USB-serial framing shared by explicit JoshuaWire v1/v2 artifacts
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
// frame (header first to learn `len`, then the rest). Returns the byte count,
// or zero on timeout/invalid size. The endpoint validates version and CRC.
size_t TransportReadFrame(uint8_t* frame_buf, size_t frame_buf_cap);

void TransportWriteFrame(const uint8_t* frame, size_t len);

#ifdef __cplusplus
}
#endif
