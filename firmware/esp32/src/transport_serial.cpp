#include "transport_serial.h"

#include <Arduino.h>

namespace {
constexpr unsigned long kByteTimeoutMs = 20;
}  // namespace

void TransportInit(void) {
  // Unlike Teensy 4.1's native USB CDC (which ignores the baud rate
  // argument entirely), most ESP32 dev boards reach the host through a
  // real UART routed to a USB-to-serial bridge chip (CP2102/CH340/...),
  // so this baud rate is real and must match the host's
  // serial_config.baudrate in the pbtxt (docs/BOARD_LAYER_RFC.md §7.3).
  // ESP32-S2/S3 boards with native USB CDC would ignore it the same way
  // Teensy does, but the call is harmless either way.
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
