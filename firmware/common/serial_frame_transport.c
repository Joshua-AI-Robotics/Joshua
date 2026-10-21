#include "serial_frame_transport.h"

#include <string.h>

static void AdvanceTransmit(serial_frame_transport_t* state) {
  if (state->transmit_length == 0) return;
  const size_t remaining = state->transmit_length - state->transmit_offset;
  const size_t sent = state->config.write_bytes(
      state->config.context, state->transmit + state->transmit_offset, remaining);
  state->transmit_offset += sent;
  if (state->transmit_offset == state->transmit_length) {
    state->transmit_length = 0;
    state->transmit_offset = 0;
  }
}

static frame_status_t PollReceive(void* context, uint8_t* frame, size_t capacity, size_t* length) {
  if (length == NULL) return FRAME_ERROR;
  *length = 0;
  if (frame == NULL || capacity < JW_MAX_FRAME_LEN) return FRAME_ERROR;
  serial_frame_transport_t* state = (serial_frame_transport_t*)context;
  AdvanceTransmit(state);
  for (size_t budget = 0; budget < JW_MAX_FRAME_LEN; ++budget) {
    const int byte = state->config.read_byte(state->config.context);
    if (byte < 0) break;
    const frame_status_t status =
        serial_frame_assembler_push(&state->assembler,
                                    state->config.now_ms(state->config.context),
                                    (uint8_t)byte,
                                    frame,
                                    capacity,
                                    length);
    if (status != FRAME_NO_DATA) return status;
  }
  return FRAME_NO_DATA;
}

static frame_status_t TrySend(void* context, const uint8_t* frame, size_t length) {
  if (frame == NULL || length == 0 || length > JW_MAX_FRAME_LEN) return FRAME_ERROR;
  serial_frame_transport_t* state = (serial_frame_transport_t*)context;
  AdvanceTransmit(state);
  if (state->transmit_length != 0) return FRAME_WOULD_BLOCK;
  memcpy(state->transmit, frame, length);
  state->transmit_length = length;
  AdvanceTransmit(state);
  return FRAME_OK;
}

frame_transport_t serial_frame_transport_init(serial_frame_transport_t* state,
                                              const serial_frame_transport_config_t* config) {
  memset(state, 0, sizeof(*state));
  state->config = *config;
  serial_frame_assembler_init(&state->assembler, config->byte_timeout_ms);
  const frame_transport_t transport = {state, PollReceive, TrySend};
  return transport;
}
