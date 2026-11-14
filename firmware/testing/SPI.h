#pragma once
#include <cstdint>
constexpr uint8_t SPI_MODE0 = 0, SPI_MODE1 = 4, SPI_MODE2 = 8, SPI_MODE3 = 12;
constexpr uint8_t MSBFIRST = 1, VSPI = 0, HSPI = 1;
struct SPISettings {
  uint32_t speed;
  uint8_t order, mode;
  SPISettings(uint32_t s, uint8_t o, uint8_t m) : speed(s), order(o), mode(m) {}
};
class SPIClass {
 public:
  explicit SPIClass(int = 0) {}
  unsigned begins = 0, transactions = 0, ends = 0;
  uint8_t mode = 0;
  int clock = -1, mosi = -1, miso = -1;
  void setSCK(int p) {
    clock = p;
  }
  void setMOSI(int p) {
    mosi = p;
  }
  void setMISO(int p) {
    miso = p;
  }
  void begin() {
    ++begins;
  }
  void begin(int c, int i, int o, int) {
    clock = c;
    miso = i;
    mosi = o;
    ++begins;
  }
  void end() {
    ++ends;
  }
  void beginTransaction(SPISettings s) {
    mode = s.mode;
    ++transactions;
  }
  void endTransaction() {}
  uint8_t transfer(uint8_t v) {
    return v ^ 0x80;
  }
};
inline SPIClass SPI, SPI1;
