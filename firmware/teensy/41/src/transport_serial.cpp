#include "transport_serial.h"

#include <Arduino.h>
#include <string.h>

#include "joshua_wire_stream.h"

namespace {
jw_stream_decoder_t decoder;
}
void TransportInit() {
  Serial.begin(115200);
  jw_stream_init(&decoder);
}
size_t TransportReadFrame(uint8_t* frame, size_t capacity) {
  // Bound work per loop even if the producer continuously fills the UART.
  for (size_t i = 0; i < JW_MAX_FRAME_LEN && Serial.available() > 0; ++i) {
    const int size = jw_stream_feed(&decoder, Serial.read(), millis());
    if (size > 0) {
      if (static_cast<size_t>(size) > capacity) return 0;
      memcpy(frame, decoder.bytes, size);
      return size;
    }
  }
  return 0;
}
void TransportWriteFrame(const uint8_t* frame, size_t size) {
  Serial.write(frame, size);
}
