// Version-neutral JoshuaWire command IDs, semantic types and payload definitions.
// Shared by host and MCU firmware using JoshuaWire 0.0.2; framing,
// frame layouts and session state belong to their respective codec/endpoint.
// Structs describe values, not packed wire images: codecs serialize explicitly.
#pragma once

#include <stddef.h>
#include <stdint.h>

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
#define JW_SET_TARGET_PAYLOAD_LEN 5
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

// Mirrors robot::board::TargetMode (robot/board/interfaces/board_channel.h)
// value-for-value so the host casts directly with no lookup table.
typedef enum {
  JW_MODE_POSITION = 0,
  JW_MODE_VELOCITY = 1,
  JW_MODE_TORQUE = 2,
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
typedef enum {
  JW_BOARD_INVALID = 0,
  JW_BOARD_AM243 = 1,
  JW_BOARD_TEENSY41 = 2,
  JW_BOARD_ARDUINO_UNO = 3,
  JW_BOARD_ESP32 = 8,
} jw_board_id_t;

typedef enum {
  JW_STATUS_OK = 0,
  JW_STATUS_ERROR = 1,
  JW_STATUS_UNSUPPORTED = 2,
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

typedef struct {
  jw_mode_t mode;
  float value;
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
// count, decoders return 0; all return -1 for null pointers or invalid size.
// Decoders require the exact payload length. No frame headers, CRCs or IDs are
// read/written. Dispatch/session code validates command/channel/correlation;
// drive handlers retain responsibility for supported modes and safety policy.
int jw_encode_status_payload(uint8_t* out, size_t cap, jw_status_t status);
int jw_decode_status_payload(const uint8_t* data, size_t len, jw_status_t* out);
int jw_encode_identify_payload(uint8_t* out, size_t cap, const jw_identify_response_t* value);
int jw_decode_identify_payload(const uint8_t* data, size_t len, jw_identify_response_t* out);
int jw_encode_feedback_payload(uint8_t* out, size_t cap, const jw_feedback_t* value);
int jw_decode_feedback_payload(const uint8_t* data, size_t len, jw_feedback_t* out);
int jw_encode_set_target_payload(uint8_t* out, size_t cap, jw_mode_t mode, float value);
int jw_decode_set_target_payload(const uint8_t* data, size_t len, jw_set_target_t* out);
int jw_encode_configure_step_dir_payload(uint8_t* out,
                                         size_t cap,
                                         const jw_configure_step_dir_t* value);
int jw_decode_configure_step_dir_payload(const uint8_t* data,
                                         size_t len,
                                         jw_configure_step_dir_t* out);

#ifdef __cplusplus
}
#endif
