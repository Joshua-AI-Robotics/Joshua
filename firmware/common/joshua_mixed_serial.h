// Allocation-free arbitration around the existing MCU endpoint and frame I/O.
#pragma once
#include "frame_transport.h"
#include "joshua_wire.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  void* context;
  void (*service)(void*);  // Watchdogs, motor and bounded acquisition first.
  int (*dispatch)(void*, const uint8_t*, size_t, uint8_t*, size_t);
  void (*fault)(void*);
  int (*sample)(void*, uint8_t*, size_t, uint32_t*);
  void (*submitted)(void*, uint32_t);
} jw_mixed_serial_ops_t;
typedef struct {
  frame_transport_t transport;
  jw_mixed_serial_ops_t ops;
  uint8_t ordinary[JW_MAX_FRAME_LEN], safety[JW_MAX_FRAME_LEN];
  size_t ordinary_len, safety_len;
  uint32_t admission_faults, received, sent_replies, sent_samples;
} jw_mixed_serial_t;
void jw_mixed_serial_init(jw_mixed_serial_t*, frame_transport_t, jw_mixed_serial_ops_t);
void jw_mixed_serial_service(jw_mixed_serial_t*);
#ifdef __cplusplus
}
#endif
