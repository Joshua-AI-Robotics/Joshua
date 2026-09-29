// Portable AM243 JW2 EtherCAT object/PDO endpoint and software safety policy.
// The TI bridge serializes all calls and supplies monotonic microseconds. No
// SDK, allocation, threads or GPIO here; native tests use this same code.
#pragma once

#include "joshua_commands.h"
#include "joshua_wire_ethercat.h"
#include "joshua_wire_v2_firmware_session.h"

#ifdef __cplusplus
extern "C" {
#endif

#define JOSHUA_ECAT_FAULT_COMM 1u
#define JOSHUA_ECAT_FAULT_TARGET 2u
#define JOSHUA_ECAT_FAULT_STATE 4u
#define JOSHUA_ECAT_FAULT_PROTOCOL 8u

typedef struct {
  JoshuaChannel channel;
  jw2_firmware_session_t session;
  uint8_t mailbox[JWEC_MAILBOX_SIZE];
  uint8_t input[JWEC_PDO_SIZE];
  uint8_t last_request[2][JW2_MAX_FRAME_LEN];
  uint8_t last_request_len[2];
  uint32_t last_generation[2];
  uint64_t now_us;
  uint64_t last_progress_us;
  uint64_t last_target_us;
  uint32_t comm_timeout_us;
  uint32_t target_timeout_us;
  bool operational;
} JoshuaEthercatProfile;

// Required, nonzero watchdog limits; clock must be monotonic. Call Tick from
// an independent periodic task as well as the stack loop/callbacks. Only NEW
// valid commands refresh progress; rereading a retained PDO never feeds it.
int JoshuaEthercatProfileInit(JoshuaEthercatProfile*, uint32_t comm_us, uint32_t target_us);
void JoshuaEthercatProfileTick(JoshuaEthercatProfile*, uint64_t now_us, bool operational);
void JoshuaEthercatProfileFault(JoshuaEthercatProfile*, uint16_t fault);
// Exact sizes, subindex zero, no Complete Access. Return 0 success, -1 reject.
int JoshuaEthercatProfileRead(JoshuaEthercatProfile*, uint16_t index, uint8_t* out, size_t size);
int JoshuaEthercatProfileWrite(JoshuaEthercatProfile*,
                               uint16_t index,
                               const uint8_t* data,
                               size_t size);
// Complete 80-byte snapshot in/out. Input remains retained until acknowledged.
int JoshuaEthercatProfilePdo(JoshuaEthercatProfile*, const uint8_t* output, size_t size);

#ifdef __cplusplus
}
#endif
