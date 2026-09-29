// JoshuaWire v2 EtherCAT layout-v1 wire contract, shared by host and all board
// firmware endpoints. Constants are byte offsets/sizes, never packed structs.
// This header does not implement an object dictionary, watchdog or motor I/O.
#pragma once

#include "joshua_wire_v2.h"

#define JWEC_DESCRIPTOR_INDEX 0x2000
#define JWEC_SESSION_INDEX 0x2001
#define JWEC_REQUEST_INDEX 0x2010
#define JWEC_RESPONSE_INDEX 0x2011
#define JWEC_ACK_INDEX 0x2012
#define JWEC_DESCRIPTOR_SIZE 36
#define JWEC_SESSION_SIZE 8
#define JWEC_MAILBOX_SIZE 76
#define JWEC_PDO_SIZE 80
#define JWEC_LAYOUT_VERSION 1
#define JWEC_DESCRIPTOR_VERSION 1

// Descriptor: magic[4], version:u16, min/max protocol:u16, layout:u16,
// out/in size:u16, frame limit:u16, transports:u32, artifact[12], reserved:u16.
#define JWEC_TRANSPORT_SERIAL (1u << 0)
#define JWEC_TRANSPORT_COE (1u << 1)
#define JWEC_TRANSPORT_PDO (1u << 2)
#define JWEC_RESET_OPERATION 1u
#define JWEC_TRANSPORT_STATUS_OK 0u

// Both envelopes start with session:u32 and generation:u32. Mailbox adds
// length:u16, reserved:u16, frame[64]. PDO adds acknowledgment (output) or
// response generation (input):u32, length:u16, flags/status:u16, frame[64].
#define JWEC_SESSION_OFFSET 0
#define JWEC_GENERATION_OFFSET 4
#define JWEC_MAILBOX_LENGTH_OFFSET 8
#define JWEC_MAILBOX_FRAME_OFFSET 12
#define JWEC_PDO_RESPONSE_OFFSET 8
#define JWEC_PDO_LENGTH_OFFSET 12
#define JWEC_PDO_FRAME_OFFSET 16
