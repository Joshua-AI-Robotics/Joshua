// Board-specific nonblocking UART/USB binding selected by the serial profile.
#pragma once

#include "serial_frame_transport.h"

using SerialTransport = serial_frame_transport_t;

// Hardware settings belong to this adapter, not the JW endpoint.
struct SerialTransportConfig {
  unsigned long baud_rate;
  uint32_t byte_timeout_ms;
};
frame_transport_t TransportSerialInit(SerialTransport* state, const SerialTransportConfig& config);
