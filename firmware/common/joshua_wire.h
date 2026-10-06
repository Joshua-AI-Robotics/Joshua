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
#define JW_HEADER_BODY_LEN 11
#define JW_FRAME_OVERHEAD 15
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

// Returns the encoded length, or -1 for invalid arguments/capacity. Decode
// accepts exactly one JW frame, rejects all other versions, and borrows payload
// storage from buf. Neither operation allocates memory.
int jw_encode_frame(uint8_t* buf,
                    size_t cap,
                    uint32_t session_id,
                    uint32_t message_id,
                    uint8_t cmd,
                    uint8_t channel,
                    const uint8_t* payload,
                    uint8_t payload_len);
int jw_decode_frame(const uint8_t* buf, size_t len, jw_frame_t* out);

// Responses always copy all four correlation fields from the request.
int jw_encode_response(uint8_t* buf,
                       size_t cap,
                       const jw_frame_t* request,
                       const uint8_t* payload,
                       uint8_t payload_len);
int jw_response_matches(const jw_frame_t* request, const jw_frame_t* response);

#ifdef __cplusplus
}
#endif
