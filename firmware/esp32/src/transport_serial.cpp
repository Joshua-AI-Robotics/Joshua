#include "transport_serial.h"

#include <Arduino.h>

namespace {
int ReadByte(void*) {
  return Serial.available() > 0 ? Serial.read() : -1;
}
size_t WriteBytes(void*, const uint8_t* bytes, size_t length) {
  // Sole Serial writer. Only write bytes that fit immediately; the shared link
  // adapter owns accepted frames and retains any unwritten remainder.
  const int available = Serial.availableForWrite();
  if (available <= 0) return 0;
  const size_t count = length < static_cast<size_t>(available) ? length : available;
  return Serial.write(bytes, count);
}
uint32_t NowMs(void*) {
  return millis();
}
}  // namespace

frame_transport_t TransportSerialInit(SerialTransport* state, const SerialTransportConfig& config) {
  // Use the UART FIFO directly; no ring-buffer enqueue waits for space.
  Serial.setTxBufferSize(0);
  Serial.begin(config.baud_rate);
  const serial_frame_transport_config_t binding = {
      nullptr, ReadByte, WriteBytes, NowMs, config.byte_timeout_ms};
  return serial_frame_transport_init(state, &binding);
}
