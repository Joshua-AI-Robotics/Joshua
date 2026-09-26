// JoshuaWire v2 framing, shared by host and firmware. Command payloads retain
// their v1 encoding; v2 adds correlation without reinterpreting v1 wire bytes.
// Declares wire limits, the decoded frame view and stateless codec functions;
// it does not own transport I/O, session state or board command execution.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "joshua_wire_commands.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JW2_SYNC_BYTE 0xA5
#define JW2_PROTO_VERSION 2
#define JW2_MAX_FRAME_LEN 64
#define JW2_HEADER_BODY_LEN 11
#define JW2_FRAME_OVERHEAD 15
#define JW2_MAX_PAYLOAD_LEN (JW2_MAX_FRAME_LEN - JW2_FRAME_OVERHEAD)

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
} jw2_frame_t;

// Returns the encoded length, or -1 for invalid arguments/capacity. Decode
// accepts exactly one v2 frame, rejects all other versions, and borrows payload
// storage from buf. Neither operation allocates memory.
int jw2_encode_frame(uint8_t* buf,
                     size_t cap,
                     uint32_t session_id,
                     uint32_t message_id,
                     uint8_t cmd,
                     uint8_t channel,
                     const uint8_t* payload,
                     uint8_t payload_len);
int jw2_decode_frame(const uint8_t* buf, size_t len, jw2_frame_t* out);

// Responses always copy all four correlation fields from the request.
int jw2_encode_response(uint8_t* buf,
                        size_t cap,
                        const jw2_frame_t* request,
                        const uint8_t* payload,
                        uint8_t payload_len);
int jw2_response_matches(const jw2_frame_t* request, const jw2_frame_t* response);

#ifdef __cplusplus
}
#endif
