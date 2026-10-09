// DRAFT: executable generic-device proposal, not an assigned wire standard.
// See docs/JOSHUA_WIRE_GENERIC_DEVICE_PROPOSAL.md before adopting these IDs.
// Optional profiles describe functions; no actuator/sensor union is required.
#pragma once

#include "firmware/common/joshua_wire.h"

#ifdef __cplusplus
extern "C" {
#endif

// Additive experimental commands. Legacy IDENTIFY/RESET/ESTOP retain their bytes.
// Production use requires capability negotiation and command-ID allocation.
enum {
  JWD_INFO = 0x20,      // Board scope; empty request.
  JWD_DESCRIBE = 0x21,  // Board scope; descriptor ordinal, u16 little endian.
  JWD_STATUS = 0x22,    // Function scope; empty request.
  JWD_INVOKE = 0x23,    // Function scope; profile:u32, version:u16, op:u8, data[].
};

// Every extension response starts with one status byte. Non-OK has no body.
// Unknown status values must be treated as errors by the host.
enum {
  JWD_OK = 1,
  JWD_UNSUPPORTED = 2,
  JWD_INVALID_REQUEST = 3,
  JWD_NOT_READY = 4,
  JWD_UNAVAILABLE = 5,
  JWD_RESOURCE_CONFLICT = 6,
};

enum {
  JWD_OP_READ = 1u << 0,
  JWD_OP_WRITE = 1u << 1,
  JWD_OP_CONFIGURE = 1u << 2,
};

enum { JWD_STATE_DISABLED = 1, JWD_STATE_READY = 2, JWD_STATE_UNAVAILABLE = 3 };
enum { JWD_REASON_NONE = 0, JWD_REASON_RESET = 1, JWD_REASON_ESTOP = 2, JWD_REASON_FAULT = 3 };
// Evidence of the stop action is separate from function availability.
enum { JWD_STOP_UNKNOWN = 0, JWD_STOP_OUTPUT_DISABLED = 1, JWD_STOP_PHYSICALLY_VERIFIED = 2 };
enum { JWD_RECOVERY_CONFIGURE = 1u << 0, JWD_RECOVERY_ENABLE = 1u << 1 };

#define JWD_CONTRACT_VERSION 1
#define JWD_INFO_SIZE 22
#define JWD_DESCRIPTOR_SIZE 15
#define JWD_STATE_SIZE 11
#define JWD_INVOKE_HEADER_SIZE 7

// Semantic structs only. All wire fields have fixed widths and explicit codecs.
typedef struct {
  uint8_t contract_version;
  uint8_t wire_version;
  uint8_t max_payload;
  uint8_t channel_id_bytes;
  uint16_t descriptor_count;  // Number of (function, profile) entries, not motors.
  uint32_t vendor_id;         // Proposed registry namespace, not a BoardType enum.
  uint32_t product_id;
  uint32_t firmware_revision;
  uint32_t boot_id;  // Changes on reboot; distinct from host session ID.
} jwd_info_t;

typedef struct {
  uint8_t channel;           // Stable logical function address; 0xff reserved.
  uint16_t physical_device;  // Relates functions belonging to one physical device.
  uint32_t profile;
  uint16_t profile_version;
  uint32_t operations;
  uint16_t safety_group;  // Nonzero: shared shutdown domain; 0: unspecified.
} jwd_descriptor_t;

typedef struct {
  uint8_t channel;
  uint8_t state;
  uint8_t reason;
  uint8_t stop_evidence;
  uint8_t recovery;
  uint32_t generation;  // Board-wide state snapshot generation within this boot.
  uint16_t safety_group;
} jwd_state_t;

// These codecs validate buffer shape, not registry membership/profile semantics.
// Decoders accept unknown enum/profile values so callers can reject/skip them.
// Encode: byte count or -1. Decode: JW_RESULT_OK or JW_RESULT_ERROR.
int jwd_encode_info(uint8_t* out, size_t cap, const jwd_info_t* value);
jw_result_t jwd_decode_info(const uint8_t* data, size_t len, jwd_info_t* value);
int jwd_encode_descriptor(uint8_t* out, size_t cap, const jwd_descriptor_t* value);
jw_result_t jwd_decode_descriptor(const uint8_t* data, size_t len, jwd_descriptor_t* value);
int jwd_encode_state(uint8_t* out, size_t cap, const jwd_state_t* value);
jw_result_t jwd_decode_state(const uint8_t* data, size_t len, jwd_state_t* value);

#ifdef __cplusplus
}
#endif
