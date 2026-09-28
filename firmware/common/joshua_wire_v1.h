// joshua_wire_v1 — the shared frame codec between Joshua host boards and
// Joshua-authored MCU firmware (docs/BOARD_LAYER_RFC.md §7.2/§7.3).
//
// Deliberately lowest-common-denominator C: no malloc, no libc beyond
// <stdint.h>/<stddef.h>, explicit little-endian byte packing, pure
// encode/decode functions over caller-provided buffers. One source file is
// compiled into both the host (as a Bazel cc_library, linked into
// Am243Board/TeensyBoard/ArduinoBoard) and every Joshua firmware image (as a
// PlatformIO lib), so the two sides cannot drift silently — see the repo's
// firmware/common/BUILD and firmware/teensy/41/platformio.ini.
// Per-command frame helpers delegate payload serialization to the neutral
// joshua_wire_commands module; jw1_* APIs still consume/produce real v1 frames.
//
// Frame format:
//   [0xA5 sync][len][proto_ver][cmd][channel][payload...][crc16 LE]
// `len` counts the bytes from proto_ver through the end of payload
// (i.e. len == 3 + payload_len); crc16 is computed over those same
// `len` bytes. Total frame size on the wire is therefore len + 4.
//
// This header assumes a little-endian target (true for both the Cortex-M7
// on Teensy 4.1 and the host x86/ARM Linux build) and does not attempt to
// support big-endian MCUs.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "joshua_wire_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JW1_SYNC_BYTE 0xA5
#define JW1_PROTO_VERSION 1

#define JW1_MAX_PAYLOAD_LEN 32
// sync + len + proto_ver + cmd + channel + payload + crc16.
#define JW1_MAX_FRAME_LEN (5 + JW1_MAX_PAYLOAD_LEN + 2)
// sync + len + proto_ver + cmd + channel + <payload_len bytes> + crc16.
#define JW1_FRAME_LEN(payload_len) (5 + (payload_len) + 2)

// A decoded frame. `payload` points into the caller's input buffer (not
// owned, not copied) and is valid only as long as that buffer is.
typedef struct {
  uint8_t proto_ver;
  uint8_t cmd;
  uint8_t channel;
  const uint8_t* payload;
  uint8_t payload_len;
} jw1_frame_t;

// ---- Generic frame assembly / parsing -------------------------------

// Builds one frame into `buf` (capacity `cap`). Returns the frame length
// written, or -1 if `cap` is too small or `payload_len` exceeds
// JW1_MAX_PAYLOAD_LEN.
int jw1_encode_frame(uint8_t* buf,
                     size_t cap,
                     uint8_t cmd,
                     uint8_t channel,
                     const uint8_t* payload,
                     uint8_t payload_len);

// Validates sync/len/crc in `buf` (exactly one frame's worth of bytes —
// slicing a byte stream into frames is the transport's job, e.g.
// SerialFrameTransport) and fills `out`. Returns 0 on success, -1 on a
// null `buf`/`out`, or a framing or CRC error.
int jw1_decode_frame(const uint8_t* buf, size_t len, jw1_frame_t* out);

// ---- Per-command encoders (host and firmware call the same functions) --
//
// Every encoder below returns the frame length written into `buf` (>0) on
// success, or -1 on error: `cap` too small, a null required pointer
// (`buf`, or a struct-argument pointer such as `config`/`response`/
// `feedback`), or a size field out of range (documented per-function where
// it applies). There are no exceptions in this codec by design (see the
// file header) — -1 is the uniform error signal for both encode and
// decode, checked the same way a caller already checks `cap`/length.

// IDENTIFY request. No payload.
int jw1_encode_identify_request(uint8_t* buf, size_t cap);

// CONFIGURE_CHANNEL for a STEP_DIR channel: pushes pin mapping (step/dir/
// enable) and tunables, host-configured (docs/BOARD_LAYER_RFC.md §7.5).
// -1 if `config` is null.
int jw1_encode_configure_channel_step_dir(uint8_t* buf,
                                          size_t cap,
                                          uint8_t channel,
                                          const jw_configure_step_dir_t* config);

// SET_TARGET: a position/velocity/torque command for one channel.
int jw1_encode_set_target(uint8_t* buf, size_t cap, uint8_t channel, jw_mode_t mode, float value);

// GET_FEEDBACK request. No payload.
int jw1_encode_get_feedback_request(uint8_t* buf, size_t cap, uint8_t channel);

// ENABLE: arms one channel's drive output.
int jw1_encode_enable(uint8_t* buf, size_t cap, uint8_t channel);

// DISABLE: de-energizes one channel's drive output.
int jw1_encode_disable(uint8_t* buf, size_t cap, uint8_t channel);

// ESTOP: board-scope emergency stop across every channel.
int jw1_encode_estop(uint8_t* buf, size_t cap);

// Generic OK/ERROR/UNSUPPORTED reply to whichever `cmd` is being answered.
int jw1_encode_status_response(
    uint8_t* buf, size_t cap, uint8_t cmd, uint8_t channel, jw_status_t status);

// IDENTIFY reply: board id, channel count, and per-channel drive type (the
// facts IDENTIFY actually gates Init() on — docs/BOARD_LAYER_RFC.md §7.5).
// -1 if `response` is null or `response->n_channels` exceeds
// JW_MAX_CHANNELS.
int jw1_encode_identify_response(uint8_t* buf, size_t cap, const jw_identify_response_t* response);

// GET_FEEDBACK reply: position, velocity, fault flags for one channel.
// -1 if `feedback` is null.
int jw1_encode_feedback_response(uint8_t* buf,
                                 size_t cap,
                                 uint8_t channel,
                                 const jw_feedback_t* feedback);

// ---- Per-command decoders ---------------------------------------------
//
// Every decoder below takes an already-`jw1_decode_frame`-decoded `frame`
// and returns 0 on success, -1 on error: a null `frame`/`out`, `frame->cmd`
// not matching the command this decoder parses, or `frame->payload_len`
// not matching that command's fixed payload size.

// Host-side: parse what firmware sent back.
int jw1_decode_identify_response(const jw1_frame_t* frame, jw_identify_response_t* out);
int jw1_decode_feedback_response(const jw1_frame_t* frame, jw_feedback_t* out);
int jw1_decode_status_response(const jw1_frame_t* frame, jw_status_t* out);

// Firmware-side: parse what the host sent.
int jw1_decode_set_target(const jw1_frame_t* frame, jw_set_target_t* out);
int jw1_decode_configure_channel_step_dir(const jw1_frame_t* frame, jw_configure_step_dir_t* out);

#ifdef __cplusplus
}
#endif
