// Nonblocking complete-frame I/O. Hardware configuration belongs to adapters.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FRAME_OK = 0,
  FRAME_NO_DATA,
  FRAME_WOULD_BLOCK,
  FRAME_ERROR,
} frame_status_t;

// One instance represents one peer/controller connection. Calls are serialized
// by the owning loop and must not wait for traffic or delay motor servicing.
// poll_receive sets length to zero unless returning FRAME_OK with exactly one
// complete frame in caller-owned storage. It returns FRAME_NO_DATA or an error
// otherwise. An adapter may retain partial link input between calls.
// try_send accepts the ENTIRE frame (FRAME_OK), or accepts NONE of it and returns
// FRAME_WOULD_BLOCK/an error. Accepted bytes are copied or consumed before return;
// the caller may immediately reuse its buffer. Acceptance does not establish
// delivery or execution of a remote command. Link failures after acceptance
// require link-specific recovery. Neither call validates JW CRCs or sessions.
// Context and callbacks must remain alive until the owner stops using them.
typedef struct {
  void* context;
  frame_status_t (*poll_receive)(void* context, uint8_t* frame, size_t capacity, size_t* length);
  frame_status_t (*try_send)(void* context, const uint8_t* frame, size_t length);
} frame_transport_t;

#ifdef __cplusplus
}
#endif
