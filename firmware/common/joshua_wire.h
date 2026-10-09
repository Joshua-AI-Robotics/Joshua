// JoshuaWire 0.0.2 framing, shared by host and firmware. Session/message
// correlation and the validated wire format are unchanged from JW2.
// Declares wire limits, the decoded frame view and stateless codec functions;
// it does not own transport I/O, session state or board command execution.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "joshua_wire_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JW_VERSION_MAJOR 0
#define JW_VERSION_MINOR 0
#define JW_VERSION_PATCH 2
#define JW_VERSION_STRING "0.0.2"

// The on-wire revision is distinct from the semantic release version.
#define JW_SYNC_BYTE 0xA5
#define JW_PROTO_VERSION 2
#define JW_MAX_FRAME_LEN 64
#define JW_LENGTH_OFFSET 1
#define JW_LENGTH_PREFIX_LEN (JW_LENGTH_OFFSET + 1)  // Sync and length bytes.
#define JW_CRC_LEN 2
#define JW_HEADER_BODY_LEN 11
// Bytes excluded from the wire length field: sync, length and CRC16.
#define JW_LENGTH_FIELD_OVERHEAD (JW_LENGTH_PREFIX_LEN + JW_CRC_LEN)
// Bytes outside the payload: prefix, fixed header body and CRC16.
#define JW_FRAME_OVERHEAD (JW_HEADER_BODY_LEN + JW_LENGTH_FIELD_OVERHEAD)
#define JW_MIN_FRAME_LEN JW_FRAME_OVERHEAD
#define JW_MAX_PAYLOAD_LEN (JW_MAX_FRAME_LEN - JW_FRAME_OVERHEAD)

// [sync][len][version][session:u32le][message:u32le][cmd][channel]
// [payload...][crc16le]. len and CRC cover version through payload, including
// both IDs. CRC is CRC-16/CCITT-FALSE. IDs must be nonzero in both directions.
typedef struct {
  uint8_t proto_ver;
  uint32_t session_id;
  uint32_t message_id;
  uint8_t cmd;
  uint8_t channel;
  const uint8_t* payload;
  uint8_t payload_len;
} jw_frame_t;

// Encode returns the encoded length, or -1 for invalid arguments/capacity.
// Decode returns JW_RESULT_OK or JW_RESULT_ERROR. It accepts exactly one JW
// frame, rejects all other versions, and borrows payload storage from buf.
// Neither operation allocates memory.
int jw_encode_frame(uint8_t* buf,
                    size_t cap,
                    uint32_t session_id,
                    uint32_t message_id,
                    uint8_t cmd,
                    uint8_t channel,
                    const uint8_t* payload,
                    uint8_t payload_len);
jw_result_t jw_decode_frame(const uint8_t* buf, size_t len, jw_frame_t* out);

// Responses always copy all four correlation fields from the request.
int jw_encode_response(uint8_t* buf,
                       size_t cap,
                       const jw_frame_t* request,
                       const uint8_t* payload,
                       uint8_t payload_len);
// Local correlation diagnostics, not on-wire status or device fault flags.
typedef enum {
  JW_MATCH_OK = 0,
  JW_MATCH_NULL_ARGUMENT,
  JW_MATCH_INVALID_VERSION,
  JW_MATCH_INVALID_SESSION_ID,
  JW_MATCH_INVALID_MESSAGE_ID,
  JW_MATCH_SESSION_MISMATCH,
  JW_MATCH_MESSAGE_MISMATCH,
  JW_MATCH_COMMAND_MISMATCH,
  JW_MATCH_CHANNEL_MISMATCH,
} jw_match_result_t;

// Returns the first failure in the order above, for caller-owned logging/fault
// reporting. The codec itself performs no logging or device I/O.
// TODO: Have callers log or raise an appropriate fault for non-JW_MATCH_OK
// results, including the mismatch reason and request/response correlation IDs.
jw_match_result_t jw_check_response(const jw_frame_t* request, const jw_frame_t* response);
// Boolean convenience wrapper: 1 for JW_MATCH_OK, otherwise 0.
int jw_response_matches(const jw_frame_t* request, const jw_frame_t* response);

#ifdef __cplusplus
}
#endif
