// Version-neutral JoshuaWire command IDs, semantic types and payload definitions.
// Shared by host and MCU firmware using JoshuaWire 0.1.0 (wire revision 3); framing,
// frame layouts and session state belong to their respective codec/endpoint.
// Structs describe values, not packed wire images: codecs serialize explicitly.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "joshua_wire_actuator.h"
#include "joshua_wire_io.h"
#include "joshua_wire_sensor.h"

// Local codec result, not an on-wire jw_status_t value.
typedef enum {
  JW_RESULT_OK = 0,
  JW_RESULT_ERROR = -1,
} jw_result_t;

// Board-scope commands (IDENTIFY, ESTOP, RESET_SESSION) use this channel byte.
#define JW_CHANNEL_NONE 0xFF

#define JW_MAX_CHANNELS 8
#define JW_FW_NAME_LEN 16
// Revision 3 IDENTIFY sends only n_channels descriptors. Revision 1/2's drive-only
// layout is incompatible; JW1 codecs retain their own types and fixed sizes.
#define JW_CHANNEL_ID_OFFSET 0
#define JW_CHANNEL_KIND_OFFSET (JW_CHANNEL_ID_OFFSET + 1)
#define JW_CHANNEL_CAPABILITIES_OFFSET (JW_CHANNEL_KIND_OFFSET + 1)
#define JW_CHANNEL_INTERFACE_OFFSET (JW_CHANNEL_CAPABILITIES_OFFSET + 4)
#define JW_CHANNEL_DESCRIPTOR_WIRE_LEN (JW_CHANNEL_INTERFACE_OFFSET + 1)
#define JW_IDENTIFY_BOARD_OFFSET 0
#define JW_IDENTIFY_NAME_OFFSET (JW_IDENTIFY_BOARD_OFFSET + 1)
#define JW_IDENTIFY_COUNT_OFFSET (JW_IDENTIFY_NAME_OFFSET + JW_FW_NAME_LEN)
#define JW_IDENTIFY_CHANNELS_OFFSET (JW_IDENTIFY_COUNT_OFFSET + 1)
#define JW_IDENTIFY_MIN_PAYLOAD_LEN JW_IDENTIFY_CHANNELS_OFFSET
#define JW_IDENTIFY_MAX_PAYLOAD_LEN \
  (JW_IDENTIFY_MIN_PAYLOAD_LEN + JW_MAX_CHANNELS * JW_CHANNEL_DESCRIPTOR_WIRE_LEN)
#define JW_STATUS_RESPONSE_PAYLOAD_LEN 1

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
  // Sensor sample / I/O value read: empty request, interface-specific response.
  JW_CMD_READ_CHANNEL = 0x09,
  // I/O value write: interface-specific request, status response.
  JW_CMD_WRITE_CHANNEL = 0x0A,
} jw_cmd_t;

// A channel describes a device or I/O function, not necessarily a motor drive.
typedef enum {
  JW_CHANNEL_KIND_INVALID = 0,
  JW_CHANNEL_KIND_ACTUATOR = 1,
  JW_CHANNEL_KIND_SENSOR = 2,
  JW_CHANNEL_KIND_IO = 3,
} jw_channel_kind_t;

typedef enum {
  JW_CAP_CONFIGURE = 1u << 0,
  JW_CAP_READ = 1u << 1,
  JW_CAP_WRITE = 1u << 2,
  // Reserved for interface-specific streaming schemas/handlers; the current
  // request-response endpoint does not provide unsolicited streaming.
  JW_CAP_STREAM = 1u << 3,
  JW_CAP_ENABLE_DISABLE = 1u << 4,
} jw_channel_capability_t;

typedef struct {
  // IDs may be sparse, but must be unique and cannot equal JW_CHANNEL_NONE.
  uint8_t channel_id;
  jw_channel_kind_t kind;
  uint32_t capabilities;  // Preserve unknown bits for future extensions.
  // Only the member selected by kind is meaningful. Interface IDs are scoped
  // to that kind; new interfaces must extend the enum, codec and command checks.
  union {
    jw_drive_t actuator_drive;
    jw_sensor_interface_t sensor_interface;
    jw_io_interface_t io_interface;
  } interface;
} jw_channel_descriptor_t;

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
  // Only the first n_channels entries are advertised; decode clears the rest.
  jw_channel_descriptor_t channels[JW_MAX_CHANNELS];
} jw_identify_response_t;

#ifdef __cplusplus
extern "C" {
#endif

// Revision 3 payload codecs. Encoders return the byte
// count or -1 on failure. Decoders return JW_RESULT_OK on success, or
// JW_RESULT_ERROR for null pointers, invalid sizes or malformed payloads.
// Decoders require the exact payload length. No frame headers, CRCs or IDs are
// read/written. Dispatch/session code validates command/channel/correlation;
// device handlers retain responsibility for interface-specific configuration,
// supported modes and safety policy. CONFIGURE_CHANNEL uses the advertised
// kind/interface; only actuator STEP_DIR configuration is defined currently.
// Do not advertise CONFIGURE/STREAM without a matching schema and handler.
int jw_encode_status_payload(jw_status_t status, uint8_t* out, size_t cap);
jw_result_t jw_decode_status_payload(const uint8_t* data, size_t len, jw_status_t* out);
// IDENTIFY: [board:u8][name:16][count:u8], followed by count descriptors:
// [id:u8][kind:u8][capabilities:u32le][selected interface:u8]. No struct padding.
// Returns zero for counts exceeding JW_MAX_CHANNELS.
size_t jw_identify_payload_len(uint8_t n_channels);
int jw_encode_identify_payload(uint8_t* out, size_t cap, const jw_identify_response_t* value);
jw_result_t jw_decode_identify_payload(const uint8_t* data,
                                       size_t len,
                                       jw_identify_response_t* out);
// Channel commands must match both kind and capability. Board/session commands
// are handled separately. This helper does not apply device safety/units policy.
int jw_channel_supports_command(const jw_channel_descriptor_t* channel, uint8_t cmd);
int jw_encode_sensor_sample_payload(uint8_t* out,
                                    size_t cap,
                                    jw_sensor_interface_t interface,
                                    const jw_sensor_sample_t* value);
jw_result_t jw_decode_sensor_sample_payload(const uint8_t* data,
                                            size_t len,
                                            jw_sensor_interface_t interface,
                                            jw_sensor_sample_t* out);
int jw_encode_io_payload(uint8_t* out, size_t cap, const jw_io_value_t* value);
jw_result_t jw_decode_io_payload(const uint8_t* data,
                                 size_t len,
                                 jw_io_interface_t interface,
                                 jw_io_value_t* out);
// Actuator-only target/feedback operations; never reinterpret sensor/I/O data.
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
