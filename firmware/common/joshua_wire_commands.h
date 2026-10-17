// JoshuaWire core contracts. Device schemas live in versioned profiles.
// Semantic structs are never packed/copied onto the wire. See docs/JOSHUA_WIRE.md.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { JW_RESULT_OK = 0, JW_RESULT_ERROR = -1 } jw_result_t;
#define JW_CHANNEL_NONE 0xff
#define JW_FW_NAME_LEN 16
#define JW_MAX_FUNCTIONS 255
#define JW_DISCOVERY_PAGE_SIZE 4
#define JW_CURSOR_END 0xffff
#define JW_PAYLOAD_LIMIT 81  // Revision 4: 96-byte frame minus 15-byte envelope.
#define JW_REPLY_HEADER_LEN 2
#define JW_PROFILE_HEADER_LEN 5
#define JW_FUNCTION_DESCRIPTOR_LEN 13
#define JW_DISCOVERY_HEADER_LEN 9
#define JW_DISCOVERY_MAX_LEN \
  (JW_DISCOVERY_HEADER_LEN + JW_DISCOVERY_PAGE_SIZE * JW_FUNCTION_DESCRIPTOR_LEN)
#define JW_IDENTIFY_PAYLOAD_LEN 52
#define JW_DEVICE_STATUS_LEN 19
#define JW_DEPENDENCY_PAGE_SIZE 16
#define JW_DEPENDENCY_HEADER_LEN 7
#define JW_DEPENDENCY_RECORD_LEN 4
#define JW_DEPENDENCY_MAX_LEN \
  (JW_DEPENDENCY_HEADER_LEN + JW_DEPENDENCY_PAGE_SIZE * JW_DEPENDENCY_RECORD_LEN)

typedef enum {
  JW_CMD_INVALID = 0,
  JW_CMD_IDENTIFY = 0x01,
  // 0x02..0x06, 0x09..0x0a were device commands in retired revisions.
  JW_CMD_ESTOP = 0x07,
  JW_CMD_RESET_SESSION = 0x08,
  JW_CMD_PROFILE = 0x0b,
  JW_CMD_DISCOVER = 0x0c,
  JW_CMD_DESCRIBE_FUNCTION = 0x0d,
  JW_CMD_GET_STATUS = 0x0e,
  JW_CMD_GET_DEPENDENCIES = 0x0f,
  JW_CMD_RECOVER = 0x10,
} jw_cmd_t;

typedef struct {
  uint32_t session_id;  // Action token for asynchronous completion/status.
  uint32_t message_id;
  uint8_t cmd;
  uint8_t channel;  // Stable function ID, or JW_CHANNEL_NONE for board scope.
  const uint8_t* payload;
  size_t payload_len;
} jw_command_t;

typedef enum {
  JW_STATUS_INVALID = 0,
  JW_STATUS_OK = 1,
  JW_STATUS_ERROR = 2,  // Confirmed operation failure; reconcile partial effects.
  JW_STATUS_UNSUPPORTED = 3,
  JW_STATUS_INVALID_CHANNEL = 4,
  JW_STATUS_INVALID_PAYLOAD = 5,
  JW_STATUS_UNAVAILABLE = 6,
  JW_STATUS_NOT_READY = 7,
  JW_STATUS_BUSY = 8,  // Rejected before execution, safe to try with a NEW ID.
  JW_STATUS_RESOURCE_CONFLICT = 9,
  JW_STATUS_PROFILE_MISMATCH = 10,
  JW_STATUS_STALE_DISCOVERY = 11,
  JW_STATUS_OUTCOME_UNKNOWN = 12,  // Execution may have occurred; never blind-retry.
} jw_status_t;

typedef enum {
  JW_EXECUTION_REJECTED = 0,
  JW_EXECUTION_ACCEPTED = 1,  // Bounded asynchronous work queued, not complete.
  JW_EXECUTION_COMPLETE = 2,  // Firmware action complete, not physical safety.
  JW_EXECUTION_UNKNOWN = 3,
} jw_execution_t;

typedef struct {
  jw_status_t status;
  jw_execution_t execution;
  const uint8_t* data;  // Borrowed, excludes the common two-byte header.
  size_t data_len;
} jw_reply_t;

typedef struct {
  uint8_t function_id;
  uint16_t device_id;   // Functions on one physical device share this stable ID.
  uint16_t profile_id;  // Opaque to core, zero invalid. Unknown IDs are preserved.
  uint8_t profile_major;
  uint8_t profile_minor;
  uint32_t operations;     // Bit N = profile operation N, N in 0..31.
  uint16_t safety_domain;  // Zero = board-wide; see dependency enumeration.
} jw_function_descriptor_t;

typedef struct {
  uint32_t vendor_id;  // Informational; compatibility never depends on these.
  uint32_t product_id;
  char fw_name[JW_FW_NAME_LEN];   // Fixed bytes, not necessarily NUL terminated.
  uint32_t boot_id;               // Changes every reboot; stable across session resets.
  uint32_t discovery_generation;  // Nonzero; changes with topology/capabilities.
  uint16_t function_count;
  uint16_t max_frame_len;
  uint8_t max_inflight;  // This revision requires exactly one across all planes.
  uint8_t max_page_entries;
  uint32_t max_handler_us;  // Declared worst-case dispatch time, nonzero.
  uint32_t safety_flags;
  uint8_t protocol_min;
  uint8_t protocol_max;
  uint32_t supported_planes;
} jw_identify_response_t;

#define JW_PLANE_MESSAGE (1u << 0)
#define JW_PLANE_CYCLIC (1u << 1)

#define JW_SAFETY_BOARD_ESTOP (1u << 0)
#define JW_SAFETY_DOMAIN_ESTOP (1u << 1)
#define JW_SAFETY_PHYSICAL_VERIFICATION (1u << 2)
#define JW_SAFETY_COMM_INTERRUPTED (1u << 3)

typedef struct {
  uint32_t generation;
  uint16_t total;
  uint16_t next_cursor;
  uint8_t count;
  jw_function_descriptor_t functions[JW_DISCOVERY_PAGE_SIZE];
} jw_discovery_page_t;

typedef struct {
  uint16_t cause_domain;
  uint8_t function_id;
  uint8_t effects;
} jw_safety_dependency_t;
#define JW_SHUTDOWN_OUTPUT (1u << 0)
#define JW_SHUTDOWN_POWER (1u << 1)
#define JW_SHUTDOWN_EXECUTION (1u << 2)
#define JW_SHUTDOWN_COMMUNICATION (1u << 3)
typedef struct {
  uint32_t generation;
  uint16_t next_cursor;
  uint8_t count;
  jw_safety_dependency_t dependencies[JW_DEPENDENCY_PAGE_SIZE];
} jw_dependency_page_t;

typedef enum {
  JW_STATE_UNKNOWN = 0,
  JW_STATE_DISABLED = 1,
  JW_STATE_READY = 2,
  JW_STATE_ACTIVE = 3,
  JW_STATE_UNAVAILABLE = 4,
  JW_STATE_FAULT = 5,
} jw_device_state_t;
typedef enum {
  JW_REASON_NONE = 0,
  JW_REASON_SESSION_RESET = 1,
  JW_REASON_ESTOP = 2,
  JW_REASON_SHUTDOWN_DEPENDENCY = 3,
  JW_REASON_DEVICE_FAULT = 4,
  JW_REASON_REBOOT = 5,
} jw_state_reason_t;
#define JW_RECOVERY_RECONFIGURE (1u << 0)
#define JW_RECOVERY_EXPLICIT_ENABLE (1u << 1)
#define JW_RECOVERY_CLEAR_ESTOP (1u << 2)
typedef enum {
  JW_STOP_NONE = 0,
  JW_STOP_ACCEPTED = 1,
  JW_STOP_ACTION_COMPLETE = 2,
} jw_stop_phase_t;
typedef enum {
  JW_SAFE_UNSUPPORTED = 0,
  JW_SAFE_UNKNOWN = 1,
  JW_SAFE_VERIFIED = 2,
  JW_SAFE_NOT_VERIFIED = 3,
} jw_safe_verification_t;
typedef struct {
  jw_device_state_t state;
  jw_state_reason_t reason;
  uint8_t recovery;
  jw_stop_phase_t stop_phase;
  jw_safe_verification_t verification;
  uint32_t epoch;  // Changes on every transition; scoped to IDENTIFY.boot_id.
  uint32_t action_session_id;
  uint32_t action_message_id;       // Last state-changing action; zero if none.
  jw_execution_t action_execution;  // Query completion of accepted work.
  jw_status_t action_status;        // INVALID only when there is no action token.
} jw_device_status_t;

typedef struct {
  uint16_t profile_id;
  uint8_t major;
  uint8_t minor;
  uint8_t operation;
  const uint8_t* data;
  size_t data_len;
} jw_profile_request_t;

int jw_encode_reply_payload(uint8_t* out,
                            size_t cap,
                            jw_status_t status,
                            jw_execution_t execution,
                            const uint8_t* data,
                            size_t len);
jw_result_t jw_decode_reply_payload(const uint8_t* data, size_t len, jw_reply_t* out);
int jw_encode_identify_payload(uint8_t* out, size_t cap, const jw_identify_response_t* value);
jw_result_t jw_decode_identify_payload(const uint8_t* data,
                                       size_t len,
                                       jw_identify_response_t* out);
int jw_encode_function_descriptor(uint8_t* out, size_t cap, const jw_function_descriptor_t* value);
jw_result_t jw_decode_function_descriptor(const uint8_t* data,
                                          size_t len,
                                          jw_function_descriptor_t* out);
int jw_encode_discovery_page(uint8_t* out, size_t cap, const jw_discovery_page_t* value);
jw_result_t jw_decode_discovery_page(const uint8_t* data, size_t len, jw_discovery_page_t* out);
int jw_encode_dependency_page(uint8_t* out, size_t cap, const jw_dependency_page_t* value);
jw_result_t jw_decode_dependency_page(const uint8_t* data, size_t len, jw_dependency_page_t* out);
int jw_encode_device_status(uint8_t* out, size_t cap, const jw_device_status_t* value);
jw_result_t jw_decode_device_status(const uint8_t* data, size_t len, jw_device_status_t* out);
int jw_encode_profile_request(uint8_t* out, size_t cap, const jw_profile_request_t* value);
jw_result_t jw_decode_profile_request(const uint8_t* data, size_t len, jw_profile_request_t* out);
int jw_function_supports_operation(const jw_function_descriptor_t* function,
                                   const jw_profile_request_t* request);
#ifdef __cplusplus
}
#endif
