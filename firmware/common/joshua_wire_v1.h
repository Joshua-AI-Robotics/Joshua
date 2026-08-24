// joshua_wire_v1 — the shared frame codec between Joshua host boards and
// Joshua-authored MCU firmware (docs/BOARD_LAYER_RFC.md §7.2/§7.3).
//
// Deliberately lowest-common-denominator C: no malloc, no libc beyond
// <stdint.h>/<stddef.h>, explicit little-endian byte packing, pure
// encode/decode functions over caller-provided buffers. One source file is
// compiled into both the host (as a Bazel cc_library, linked into
// TeensyBoard/ArduinoBoard) and every Joshua firmware image (as a
// PlatformIO lib), so the two sides cannot drift silently — see the repo's
// firmware/common/BUILD, firmware/teensy/41/platformio.ini, and
// firmware/arduino/uno/platformio.ini.
//
// Frame format:
//   [0xA5 sync][len][proto_ver][cmd][channel][payload...][crc16 LE]
// `len` counts the bytes from proto_ver through the end of payload
// (i.e. len == 3 + payload_len); crc16 is computed over those same
// `len` bytes. Total frame size on the wire is therefore len + 4.
//
// This header assumes a little-endian target (true for the Cortex-M7 on
// Teensy 4.1, the ATmega328P on Uno R3, and the host x86/ARM Linux build)
// and does not attempt to support big-endian MCUs.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JW1_SYNC_BYTE 0xA5
#define JW1_PROTO_VERSION 1

// Board-scope commands (IDENTIFY, ESTOP) use this in the channel byte.
#define JW1_CHANNEL_NONE 0xFF

#define JW1_MAX_CHANNELS 8
#define JW1_FW_NAME_LEN 16
#define JW1_MAX_PAYLOAD_LEN 32
// sync + len + proto_ver + cmd + channel + payload + crc16.
#define JW1_MAX_FRAME_LEN (5 + JW1_MAX_PAYLOAD_LEN + 2)
// sync + len + proto_ver + cmd + channel + <payload_len bytes> + crc16.
#define JW1_FRAME_LEN(payload_len) (5 + (payload_len) + 2)

// Every response in the protocol has a fixed size for a given proto_ver, so
// a caller atop a fixed-size-read transport (mirrors
// robot::comm::SerialTransport::AtomicRead) always knows how much to read
// before sending. IDENTIFY's channel_drives array is therefore always
// transmitted at its maximum size (JW1_MAX_CHANNELS entries, unused slots
// padded with JW1_DRIVE_INVALID) rather than sized to the board's actual
// channel count.
#define JW1_IDENTIFY_RESPONSE_PAYLOAD_LEN (1 + JW1_FW_NAME_LEN + 1 + JW1_MAX_CHANNELS)
#define JW1_FEEDBACK_RESPONSE_PAYLOAD_LEN 10
#define JW1_STATUS_RESPONSE_PAYLOAD_LEN 1

typedef enum {
  JW1_CMD_IDENTIFY = 0x01,
  JW1_CMD_CONFIGURE_CHANNEL = 0x02,
  JW1_CMD_SET_TARGET = 0x03,
  JW1_CMD_GET_FEEDBACK = 0x04,
  JW1_CMD_ENABLE = 0x05,
  JW1_CMD_DISABLE = 0x06,
  JW1_CMD_ESTOP = 0x07,
} jw1_cmd_t;

// Mirrors robot::board::TargetMode (robot/board/interfaces/board_channel.h)
// value-for-value so the host casts directly with no lookup table.
typedef enum {
  JW1_MODE_POSITION = 0,
  JW1_MODE_VELOCITY = 1,
  JW1_MODE_TORQUE = 2,
} jw1_mode_t;

// Mirrors robot.board.DriveInterface (robot/board/proto/board.proto)
// value-for-value. Only STEP_DIR is produced by firmware today; the rest
// are reserved so a future backend can report itself without a new wire
// version.
typedef enum {
  JW1_DRIVE_INVALID = 0,
  JW1_DRIVE_STEP_DIR = 1,
  JW1_DRIVE_PWM_DC = 2,
  JW1_DRIVE_SERVO_BUS_UART = 3,
  JW1_DRIVE_CAN = 4,
  JW1_DRIVE_PDO_JOINT = 5,
} jw1_drive_t;

// Mirrors robot.board.BoardType (robot/board/proto/board.proto)
// value-for-value, for the subset of board types that run Joshua firmware.
typedef enum {
  JW1_BOARD_INVALID = 0,
  JW1_BOARD_TEENSY41 = 2,
  JW1_BOARD_ARDUINO_UNO = 3,
} jw1_board_id_t;

typedef enum {
  JW1_STATUS_OK = 0,
  JW1_STATUS_ERROR = 1,
  JW1_STATUS_UNSUPPORTED = 2,
} jw1_status_t;

// A decoded frame. `payload` points into the caller's input buffer (not
// owned, not copied) and is valid only as long as that buffer is.
typedef struct {
  uint8_t proto_ver;
  uint8_t cmd;
  uint8_t channel;
  const uint8_t* payload;
  uint8_t payload_len;
} jw1_frame_t;

typedef struct {
  jw1_board_id_t board_id;
  // Not guaranteed null-terminated if the name fills all JW1_FW_NAME_LEN
  // bytes; callers must treat this as a fixed-size buffer, not a C string.
  char fw_name[JW1_FW_NAME_LEN];
  uint8_t n_channels;
  jw1_drive_t channel_drives[JW1_MAX_CHANNELS];
} jw1_identify_response_t;

typedef struct {
  float position;
  float velocity;
  uint16_t fault_flags;
} jw1_feedback_t;

typedef struct {
  jw1_mode_t mode;
  float value;
} jw1_set_target_t;

typedef struct {
  uint32_t max_pulse_rate_hz;
  uint8_t invert_dir;
  uint8_t enable_active_low;
  // GPIO pin mapping, host-configured (docs/BOARD_LAYER_RFC.md §7.5,
  // revised — see robot/board/proto/board.proto's StepDirConfig comment
  // for why this moved out of the firmware image). MCU pin numbers, e.g.
  // Teensy 4.1 digital pin numbers.
  uint8_t step_pin;
  uint8_t dir_pin;
  uint8_t enable_pin;
  // STEP pulse HIGH width, microseconds. Host-configured, same reasoning
  // as the pins above. 0 means "use firmware's own default" (see
  // backend_stepdir.cpp) — not "zero-width pulse".
  uint16_t step_pulse_width_us;
} jw1_configure_step_dir_t;

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
                                          const jw1_configure_step_dir_t* config);

// SET_TARGET: a position/velocity/torque command for one channel.
int jw1_encode_set_target(uint8_t* buf, size_t cap, uint8_t channel, jw1_mode_t mode, float value);

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
    uint8_t* buf, size_t cap, uint8_t cmd, uint8_t channel, jw1_status_t status);

// IDENTIFY reply: board id, channel count, and per-channel drive type (the
// facts IDENTIFY actually gates Init() on — docs/BOARD_LAYER_RFC.md §7.5).
// -1 if `response` is null or `response->n_channels` exceeds
// JW1_MAX_CHANNELS.
int jw1_encode_identify_response(uint8_t* buf, size_t cap, const jw1_identify_response_t* response);

// GET_FEEDBACK reply: position, velocity, fault flags for one channel.
// -1 if `feedback` is null.
int jw1_encode_feedback_response(uint8_t* buf,
                                 size_t cap,
                                 uint8_t channel,
                                 const jw1_feedback_t* feedback);

// ---- Per-command decoders ---------------------------------------------
//
// Every decoder below takes an already-`jw1_decode_frame`-decoded `frame`
// and returns 0 on success, -1 on error: a null `frame`/`out`, `frame->cmd`
// not matching the command this decoder parses, or `frame->payload_len`
// not matching that command's fixed payload size.

// Host-side: parse what firmware sent back.
int jw1_decode_identify_response(const jw1_frame_t* frame, jw1_identify_response_t* out);
int jw1_decode_feedback_response(const jw1_frame_t* frame, jw1_feedback_t* out);
int jw1_decode_status_response(const jw1_frame_t* frame, jw1_status_t* out);

// Firmware-side: parse what the host sent.
int jw1_decode_set_target(const jw1_frame_t* frame, jw1_set_target_t* out);
int jw1_decode_configure_channel_step_dir(const jw1_frame_t* frame, jw1_configure_step_dir_t* out);

#ifdef __cplusplus
}
#endif
