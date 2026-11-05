#include "joshua_watchdogs.h"

#include <string.h>
static int valid(const jw_watchdog_policy_t* p) {
  return p && p->communication_us >= JW_WATCHDOG_MIN_US &&
         p->communication_us <= JW_WATCHDOG_MAX_US && p->target_us >= JW_WATCHDOG_MIN_US &&
         p->target_us <= JW_WATCHDOG_MAX_US;
}
int jw_encode_watchdog_policy(uint8_t* out, size_t cap, const jw_watchdog_policy_t* p) {
  if (!out || cap < JW_WATCHDOG_POLICY_LEN || !valid(p)) return -1;
  for (int i = 0; i < 4; ++i) {
    out[i] = p->communication_us >> (8 * i);
    out[4 + i] = p->target_us >> (8 * i);
  }
  return JW_WATCHDOG_POLICY_LEN;
}
jw_result_t jw_decode_watchdog_policy(const uint8_t* bytes,
                                      size_t length,
                                      jw_watchdog_policy_t* out) {
  if (!bytes || !out || length != JW_WATCHDOG_POLICY_LEN) return JW_RESULT_ERROR;
  jw_watchdog_policy_t p = {0, 0};
  for (int i = 0; i < 4; ++i) {
    p.communication_us |= (uint32_t)bytes[i] << (8 * i);
    p.target_us |= (uint32_t)bytes[4 + i] << (8 * i);
  }
  if (!valid(&p)) return JW_RESULT_ERROR;
  *out = p;
  return JW_RESULT_OK;
}
void jw_watchdogs_reset(jw_watchdogs_t* s) {
  memset(s, 0, sizeof(*s));
}
void jw_watchdogs_configure(jw_watchdogs_t* s, jw_watchdog_policy_t p, uint64_t now) {
  jw_watchdogs_reset(s);
  s->policy = p;
  s->last_request_us = now;
  s->configured = 1;
  for (int i = 0; i < JW_WATCHDOG_MAX_ACTUATORS; ++i) s->last_target_us[i] = now;
}
void jw_watchdogs_request(jw_watchdogs_t* s, uint64_t now) {
  s->last_request_us = now;
}
void jw_watchdogs_target(jw_watchdogs_t* s, uint8_t channel, uint64_t now) {
  if (channel < JW_WATCHDOG_MAX_ACTUATORS) s->last_target_us[channel] = now;
}
int jw_watchdogs_expired(
    const jw_watchdogs_t* s, uint64_t now, uint8_t enabled, uint8_t moving, int sensing) {
  if (!s->configured || (!enabled && !sensing)) return 0;
  if (now < s->last_request_us || now - s->last_request_us >= s->policy.communication_us) return 1;
  for (int i = 0; i < JW_WATCHDOG_MAX_ACTUATORS; ++i)
    if ((moving & (1u << i)) &&
        (now < s->last_target_us[i] || now - s->last_target_us[i] >= s->policy.target_us))
      return 1;
  return 0;
}
