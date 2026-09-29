// Board/stack-independent JW2 EtherCAT endpoint: CoE/PDO correlation, session
// ownership and watchdog policy. A board supplies identity and drive callbacks;
// its stack adapter supplies complete snapshots, serialization and a clock.
// No vendor SDK, allocation, threads or GPIO dependencies.
#pragma once

#include <stdbool.h>

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
  jw_identify_response_t identity;
  char artifact[12];  // Printable descriptor label, zero-padded; not a board gate.
  void* context;
  // Synchronous payload handler; must validate command payloads and never write
  // past capacity. IDENTIFY is supplied by the profile from identity above.
  int (*command)(void*, const jw_command_t*, uint8_t*, size_t);
  // Must physically disable ALL outputs and clear configuration/faults before
  // returning. Called at init and on a new SID, never on a same-SID retry.
  void (*reset)(void*);
  // Must synchronously disable ALL outputs, clear targets and latch the supplied
  // fault bits for feedback. Zero means ESTOP. Must be safe to repeat.
  void (*stop)(void*, uint16_t faults);
  // Report actual enabled state for each channel, including local drive faults.
  bool (*enabled)(void*, uint8_t channel);
} JoshuaEthercatProfileConfig;

typedef struct {
  JoshuaEthercatProfileConfig config;
  jw2_firmware_session_t session;
  uint8_t mailbox[JWEC_MAILBOX_SIZE];
  uint8_t input[JWEC_PDO_SIZE];
  uint8_t last_request[2][JW2_MAX_FRAME_LEN];
  uint8_t last_request_len[2];
  uint32_t last_generation[2];
  uint64_t now_us;
  uint64_t last_progress_us;
  uint64_t last_target_us[JW_MAX_CHANNELS];
  uint32_t comm_timeout_us;
  uint32_t target_timeout_us;
  bool operational;
  bool fault_latched;
} JoshuaEthercatProfile;

// Required, nonzero watchdog limits; clock must be monotonic. Call Tick from
// an independent periodic task as well as the stack loop/callbacks. Only NEW
// accepted commands refresh progress; target freshness is per enabled channel.
// All calls/callbacks must be externally serialized, bounded and non-reentrant.
// Config is copied; its context must outlive the profile. Initialization is for
// a fresh/stopped instance, not a way to reset a live session.
int JoshuaEthercatProfileInit(JoshuaEthercatProfile*,
                              const JoshuaEthercatProfileConfig*,
                              uint32_t comm_us,
                              uint32_t target_us);
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
