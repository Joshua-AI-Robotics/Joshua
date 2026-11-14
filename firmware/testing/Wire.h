#pragma once
#include <cstdint>
#include <vector>
class TwoWire {
 public:
  unsigned begins = 0, ends = 0;
  bool stop = true, request_stop = false, short_read = false;
  unsigned frequency = 0, timeout = 0, address = 0;
  int sda = -1, scl = -1;
  unsigned remaining = 0;
  std::vector<uint8_t> writes;
  void begin() {
    ++begins;
  }
  bool begin(int d, int c, unsigned hz) {
    sda = d;
    scl = c;
    frequency = hz;
    ++begins;
    return true;
  }
  void end() {
    ++ends;
  }
  void setSDA(int p) {
    sda = p;
  }
  void setSCL(int p) {
    scl = p;
  }
  void setClock(unsigned hz) {
    frequency = hz;
  }
  void setTimeOut(unsigned ms) {
    timeout = ms;
  }
  void beginTransmission(uint8_t a) {
    address = a;
    writes.clear();
  }
  size_t write(const uint8_t* p, size_t n) {
    writes.assign(p, p + n);
    return n;
  }
  int endTransmission(bool s) {
    stop = s;
    return 0;
  }
  unsigned requestFrom(uint8_t a, uint8_t n, uint8_t s) {
    address = a;
    remaining = short_read ? n - 1 : n;
    request_stop = s;
    return remaining;
  }
  int read() {
    if (!remaining) return -1;
    --remaining;
    return 42;
  }
};
inline TwoWire Wire, Wire1, Wire2;
