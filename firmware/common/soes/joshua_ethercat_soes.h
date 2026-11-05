// SOES binding of Joshua's shared profile, not a board or ESC hardware driver.
// SOES owns global state: exactly one instance, initialized once before polling.
#pragma once
#include "firmware/common/joshua_ethercat_profile.h"

#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
  JoshuaEthercatProfile* profile;  // Already initialized; outlives the stack.
  uint32_t vendor_id, product_code, revision, serial;
  uint64_t (*time_us)(void);
  uintptr_t (*lock)(void);
  void (*unlock)(uintptr_t);
  void (*eeprom_handler)(void);
} JoshuaSoesConfig;

// Board must initialize the ESC and confirm DL ready BEFORE calling (SOES waits
// for DL ready). Never call on a live stack. Returns -1 for invalid callbacks.
int JoshuaSoesInit(const JoshuaSoesConfig*);
// Call frequently from one task. A separate watchdog task must still tick the
// profile under the same lock; no ESC I/O takes place inside that lock.
void JoshuaSoesPoll(void);
#ifdef __cplusplus
}
#endif
