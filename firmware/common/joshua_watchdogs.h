// Opt-in core watchdog configuration and allocation-free software monitoring.
#pragma once
#include "joshua_wire_commands.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JW_WATCHDOG_POLICY_LEN 8
#define JW_WATCHDOG_MAX_ACTUATORS 8
#define JW_WATCHDOG_MIN_US 10000u
#define JW_WATCHDOG_MAX_US 10000000u

typedef struct {
  uint32_t communication_us, target_us;
} jw_watchdog_policy_t;
int jw_encode_watchdog_policy(uint8_t*, size_t, const jw_watchdog_policy_t*);
jw_result_t jw_decode_watchdog_policy(const uint8_t*, size_t, jw_watchdog_policy_t*);
typedef struct {
  jw_watchdog_policy_t policy;
  uint64_t last_request_us, last_target_us[JW_WATCHDOG_MAX_ACTUATORS];
  int configured;
} jw_watchdogs_t;
void jw_watchdogs_reset(jw_watchdogs_t*);
void jw_watchdogs_configure(jw_watchdogs_t*, jw_watchdog_policy_t, uint64_t now_us);
void jw_watchdogs_request(jw_watchdogs_t*, uint64_t now_us);
void jw_watchdogs_target(jw_watchdogs_t*, uint8_t channel, uint64_t now_us);
// Called before motor service, independently of RX/TX/acquisition progress.
// enabled_mask includes holding outputs; moving_mask needs fresh targets.
int jw_watchdogs_expired(const jw_watchdogs_t*,
                         uint64_t now_us,
                         uint8_t enabled_mask,
                         uint8_t moving_mask,
                         int sensing_active);
#ifdef __cplusplus
}
#endif
