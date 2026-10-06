// Test-only Arduino API substitute for native firmware tests. In-memory serial
// queues and recorded GPIO writes let the actual dispatch/STEP_DIR code run
// without hardware; timing is stubbed, so this does not validate pulse timing.
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int OUTPUT = 1;
inline int pin_values[256]{};
inline int pin_writes = 0;
inline unsigned long clock_us = 0;
inline void pinMode(int, int) {}
inline void digitalWrite(int pin, int value) {
  pin_values[pin] = value;
  ++pin_writes;
}
inline void delayMicroseconds(unsigned int) {}
inline unsigned long micros() {
  return clock_us;
}
inline unsigned long millis() {
  return clock_us / 1000;
}

class FakeArduinoSerial {
 public:
  void begin(int) {}
  void setTimeout(unsigned long) {}
  void setTxBufferSize(size_t) {}
  int available() {
    return input.size();
  }
  int read() {
    if (input.empty()) return -1;
    const auto byte = input.front();
    input.pop_front();
    return byte;
  }
  size_t readBytes(char* out, size_t count) {
    const size_t actual = std::min(count, input.size());
    for (size_t i = 0; i < actual; ++i) out[i] = read();
    return actual;
  }
  size_t write(const uint8_t* bytes, size_t len) {
    if (len > static_cast<size_t>(write_capacity)) return 0;
    output.insert(output.end(), bytes, bytes + len);
    return len;
  }
  int availableForWrite() {
    return write_capacity;
  }
  void flush() {}
  std::deque<uint8_t> input;
  std::vector<uint8_t> output;
  int write_capacity = 64;
};
inline FakeArduinoSerial Serial;
