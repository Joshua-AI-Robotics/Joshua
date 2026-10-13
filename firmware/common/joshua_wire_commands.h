// Version-neutral JoshuaWire command IDs, semantic types and payload definitions.
// Shared by host and MCU firmware using JoshuaWire 0.0.2; framing,
// frame layouts and session state belong to their respective codec/endpoint.
// Structs describe values, not packed wire images: codecs serialize explicitly.
#pragma once

#include <stddef.h>
#include <stdint.h>

// Local codec result, not an on-wire jw_status_t value.
typedef enum {
  JW_RESULT_OK = 0,
  JW_RESULT_ERROR = -1,
} jw_result_t;

// Board-scope commands (IDENTIFY, ESTOP, RESET_SESSION) use this channel byte.
#define JW_CHANNEL_NONE 0xFF

#define JW_MAX_CHANNELS 8
#define JW_FW_NAME_LEN 16
// Fixed payload sizes are shared across frame versions. IDENTIFY always sends
// JW_MAX_CHANNELS drive entries, padding unused slots with JW_DRIVE_INVALID.
// This also lets legacy fixed-size-read transports determine response lengths.
#define JW_IDENTIFY_RESPONSE_PAYLOAD_LEN (1 + JW_FW_NAME_LEN + 1 + JW_MAX_CHANNELS)
#define JW_FEEDBACK_RESPONSE_PAYLOAD_LEN 10
#define JW_STATUS_RESPONSE_PAYLOAD_LEN 1
#define JW_TARGET_FLOAT_WIRE_LEN 4  // Target values are serialized as f32 little-endian.
#define JW_SET_TARGET_PAYLOAD_LEN (1 + JW_TARGET_FLOAT_WIRE_LEN)
#define JW_SET_TARGET_COMPLEX_PAYLOAD_LEN (1 + 3 * JW_TARGET_FLOAT_WIRE_LEN)
#define JW_CONFIGURE_STEP_DIR_PAYLOAD_LEN 11

// Borrowed command view, independent of framing, wire version and session IDs.
// The caller owns payload storage throughout dispatch. Responses are payloads;
// the endpoint supplies the request's command/channel and any correlation IDs.
typedef struct {
  uint8_t cmd;
  uint8_t channel;
  const uint8_t* payload;
  size_t payload_len;
} jw_command_t;

typedef enum {
  JW_CMD_INVALID = 0x00,  // Reserved; zero is not a valid command.
  JW_CMD_IDENTIFY = 0x01,
  JW_CMD_CONFIGURE_CHANNEL = 0x02,
  JW_CMD_SET_TARGET = 0x03,
  JW_CMD_GET_FEEDBACK = 0x04,
  JW_CMD_ENABLE = 0x05,
  JW_CMD_DISABLE = 0x06,
  JW_CMD_ESTOP = 0x07,
  // Requires session/message correlation; required before normal commands.
  JW_CMD_RESET_SESSION = 0x08,
} jw_cmd_t;

// Wire revision 2 target modes reserve zero as invalid. Map host TargetMode
// values explicitly; these IDs differ from the legacy JW1 mode values.
typedef enum {
  JW_MODE_INVALID = 0,
  JW_MODE_POSITION = 1,
  JW_MODE_VELOCITY = 2,
  JW_MODE_TORQUE = 3,
  JW_MODE_COMPLEX = 4,
} jw_mode_t;

// Mirrors robot.board.DriveInterface (robot/board/proto/board.proto)
// value-for-value. Only STEP_DIR is produced by firmware today; the rest
// are reserved so a future backend can report itself without a new wire
// version.
typedef enum {
  JW_DRIVE_INVALID = 0,
  JW_DRIVE_STEP_DIR = 1,
  JW_DRIVE_PWM_DC = 2,
  JW_DRIVE_SERVO_BUS_UART = 3,
  JW_DRIVE_CAN = 4,
  JW_DRIVE_PDO_JOINT = 5,
} jw_drive_t;

// Stable on-wire board identities. Most match robot.board.BoardType, but the
// historical ESP32 wire ID is 8 (protobuf ESP32 is 7). Preserve that wire ID.
// Adding a board that speaks JoshuaWire:
// - Add JW_BOARD_<NAME> here with an explicit, unused ID in 1..255 (IDENTIFY
//   serializes one byte). Keep zero invalid; never renumber or reuse existing IDs.
// - Update robot/board/proto/board.proto's BoardType and host board registration
//   as needed. Map protobuf IDs explicitly; do not assume they equal wire IDs.
// - Have the board's firmware report this ID in IDENTIFY; add wire-ID and golden
//   IDENTIFY payload coverage in joshua_wire_commands_test.cc.
// - Add the matching JW1_BOARD_<NAME> in joshua_wire_v1.h only if supporting JW1.
// - Document the board under firmware/<board>/README.md and in firmware/README.md.
typedef enum {
  JW_BOARD_INVALID = 0,
  JW_BOARD_AM243 = 1,
  JW_BOARD_TEENSY41 = 2,
  JW_BOARD_ARDUINO_UNO = 3,
  JW_BOARD_ESP32 = 8,
  // Future JoshuaWire boards: JW_BOARD_<NAME> = <explicit unused u8 ID>.
} jw_board_id_t;

typedef enum {
  JW_STATUS_INVALID = 0,
  JW_STATUS_OK = 1,
  JW_STATUS_ERROR = 2,
  JW_STATUS_UNSUPPORTED = 3,
} jw_status_t;

typedef struct {
  jw_board_id_t board_id;
  // Not guaranteed null-terminated if the name fills all JW_FW_NAME_LEN
  // bytes; callers must treat this as a fixed-size buffer, not a C string.
  char fw_name[JW_FW_NAME_LEN];
  uint8_t n_channels;
  jw_drive_t channel_drives[JW_MAX_CHANNELS];
} jw_identify_response_t;

typedef struct {
  float position;
  float velocity;
  uint16_t fault_flags;
} jw_feedback_t;

// One channel's combined target, corresponding to one index in the ROS 2
// JointState position/velocity/effort arrays (effort is torque for revolute joints).
// All three fields are present; zero is a value, never an omitted field.
// Values use the channel's native units; ROS adapters must convert explicitly.
typedef struct {
  float position;
  float velocity;
  float torque;
} jw_complex_target_t;

// TODO(hmoon): Advertise supported target capabilities during identification and
// define partial-field commands (omitted != zero) and control semantics, including
// position with a speed limit. Drive handlers must validate the complete target
// before applying it and reject unsupported combinations without dropping fields.
typedef struct {
  jw_mode_t mode;
  // Used by scalar modes; zero when decoding JW_MODE_COMPLEX.
  float value;
  // Used by JW_MODE_COMPLEX; zeroed when decoding scalar modes.
  jw_complex_target_t combined;
} jw_set_target_t;

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
} jw_configure_step_dir_t;

#ifdef __cplusplus
extern "C" {
#endif

// Payload-only codecs shared by both frame versions. Encoders return the byte
// count or -1 on failure. Decoders return JW_RESULT_OK on success, or
// JW_RESULT_ERROR for null pointers, invalid sizes or malformed payloads.
// Decoders require the exact payload length. No frame headers, CRCs or IDs are
// read/written. Dispatch/session code validates command/channel/correlation;
// drive handlers retain responsibility for supported modes and safety policy.
int jw_encode_status_payload(jw_status_t status, uint8_t* out, size_t cap);
jw_result_t jw_decode_status_payload(const uint8_t* data, size_t len, jw_status_t* out);
int jw_encode_identify_payload(uint8_t* out, size_t cap, const jw_identify_response_t* value);
jw_result_t jw_decode_identify_payload(const uint8_t* data,
                                       size_t len,
                                       jw_identify_response_t* out);
int jw_encode_feedback_payload(uint8_t* out, size_t cap, const jw_feedback_t* value);
jw_result_t jw_decode_feedback_payload(const uint8_t* data, size_t len, jw_feedback_t* out);
// Scalar layout: [mode:u8][value:f32le]. JW_MODE_COMPLEX requires the dedicated
// encoder below; passing it to the scalar encoder is an error.
int jw_encode_set_target_payload(uint8_t* out, size_t cap, jw_mode_t mode, float value);
// Complex layout: [JW_MODE_COMPLEX:u8][position:f32le][velocity:f32le][torque:f32le].
// A JointState array maps to separate channel frames, without batch atomicity.
int jw_encode_complex_target_payload(uint8_t* out, size_t cap, const jw_complex_target_t* value);
// Decodes either layout, requiring the exact length for the encoded mode.
jw_result_t jw_decode_set_target_payload(const uint8_t* data, size_t len, jw_set_target_t* out);
int jw_encode_configure_step_dir_payload(uint8_t* out,
                                         size_t cap,
                                         const jw_configure_step_dir_t* value);
jw_result_t jw_decode_configure_step_dir_payload(const uint8_t* data,
                                                 size_t len,
                                                 jw_configure_step_dir_t* out);

#ifdef __cplusplus
}
#endif
