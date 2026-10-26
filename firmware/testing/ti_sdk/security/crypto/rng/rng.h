// Native subset of the TI SDK RNG interface, implemented by firmware tests.
#pragma once
#include <stdint.h>

typedef void* RNG_Handle;
typedef enum {
  RNG_RETURN_SUCCESS = 0xDE3BA502U,
  RNG_RETURN_FAILURE = 0xF33BE03EU,
} RNG_Return_t;

#ifdef __cplusplus
extern "C" {
#endif
RNG_Handle RNG_open(uint32_t index);
RNG_Return_t RNG_setup(RNG_Handle handle);
RNG_Return_t RNG_read(RNG_Handle handle, uint32_t* out);
RNG_Return_t RNG_close(RNG_Handle handle);
#ifdef __cplusplus
}
#endif
