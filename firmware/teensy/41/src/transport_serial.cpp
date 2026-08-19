#include "transport_serial.h"

#include <Arduino.h>

namespace {
constexpr unsigned long kByteTimeoutMs = 20;
}  // namespace

void TransportInit(void) {
  // Teensy's native USB CDC serial ignores the baud rate (always full
  // USB speed); passed for portability with non-USB serial variants.
  Serial.begin(115200);
  Serial.setTimeout(kByteTimeoutMs);
}

bool TransportReadFrame(jw1_frame_t* out, uint8_t* frame_buf, size_t frame_buf_cap) {
  if (frame_buf_cap < 2 || Serial.available() == 0) {
    return false;
  }

  // Resync: discard bytes already in the input buffer until sync is seen.
  // Only drains what has already arrived — never blocks waiting for a sync
  // byte that may not come.
  int sync_byte = -1;
  while (Serial.available() > 0) {
    sync_byte = Serial.read();
    if (sync_byte == JW1_SYNC_BYTE) {
      break;
    }
    sync_byte = -1;
  }
  if (sync_byte != JW1_SYNC_BYTE) {
    return false;
  }
  frame_buf[0] = static_cast<uint8_t>(sync_byte);

  if (Serial.readBytes(reinterpret_cast<char*>(frame_buf + 1), 1) != 1) {
    return false;  // Timed out waiting for `len`.
  }
  const uint8_t len = frame_buf[1];
  const size_t remaining = static_cast<size_t>(len) + 2;  // body (proto_ver..payload) + crc16.
  if (2 + remaining > frame_buf_cap) {
    return false;
  }
  if (Serial.readBytes(reinterpret_cast<char*>(frame_buf + 2), remaining) != remaining) {
    return false;  // Timed out mid-frame.
  }

  const size_t total_len = 2 + remaining;
  return jw1_decode_frame(frame_buf, total_len, out) == 0;
}

void TransportWriteFrame(const uint8_t* frame, size_t len) {
  Serial.write(frame, len);
  Serial.flush();
}
