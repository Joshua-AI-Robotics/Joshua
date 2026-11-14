// Only explicit peripheral images link hardware buses. Default images remain
// request-only STEP/DIR, with no implicit pin assignments or bus initialization.
#ifdef JOSHUA_PERIPHERAL_REFERENCE
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <string.h>

#include "joshua_peripheral_device.h"
#include "stepdir_channel.h"
#ifdef ESP32
#include <esp_arduino_version.h>
#endif
namespace {
struct Slot {
  jw_peripheral_config_t config;
  bool configured, enabled;
  uint16_t duty;
} slots[JW_PERIPHERAL_SLOTS];
#ifdef ESP32
constexpr unsigned kBoard = 2;
SPIClass spi0(VSPI), spi1(HSPI);
SPIClass* Spi(unsigned b) {
  return b ? &spi1 : &spi0;
}
TwoWire* I2c(unsigned b) {
  return b ? &Wire1 : &Wire;
}
HardwareSerial* Uart(unsigned b) {
  return b == 1 ? &Serial1 : &Serial2;
}
#else
constexpr unsigned kBoard = 1;
SPIClass* Spi(unsigned b) {
  return b ? &SPI1 : &SPI;
}
TwoWire* I2c(unsigned b) {
  return b == 0 ? &Wire : b == 1 ? &Wire1 : &Wire2;
}
HardwareSerialIMXRT* Uart(unsigned b) {
  HardwareSerialIMXRT* ports[] = {
      &Serial1, &Serial2, &Serial3, &Serial4, &Serial5, &Serial6, &Serial7, &Serial8};
  return ports[b - 1];
}
#endif
bool Other(unsigned index, uint8_t kind, uint8_t bus) {
  for (unsigned i = 0; i < JW_PERIPHERAL_SLOTS; ++i)
    if (i != index && slots[i].configured && slots[i].config.kind == kind &&
        slots[i].config.bus == bus)
      return true;
  return false;
}
void PwmWrite(unsigned i, uint16_t duty) {
  const auto pin = slots[i].config.pins[0];
#ifdef ESP32
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(pin, static_cast<uint32_t>(duty) * 4095u / 65535u);
#else
  ledcWrite(i, static_cast<uint32_t>(duty) * 4095u / 65535u);
#endif
#else
  analogWrite(pin, static_cast<uint32_t>(duty) * 4095u / 65535u);
#endif
}
jw_status_t Validate(void*, const jw_peripheral_config_t* c) {
  return jw_peripheral_route_valid(kBoard, c) ? JW_STATUS_OK : JW_STATUS_INVALID_PAYLOAD;
}
jw_status_t Configure(void*, unsigned i, const jw_peripheral_config_t* c) {
  auto& s = slots[i];
  s.config = *c;
  s.configured = true;
  s.enabled = false;
  s.duty = c->value1;
  const uint8_t p = c->pins[0];
  switch (c->kind) {
    case JW_PERIPHERAL_DIGITAL:
      if (c->flags & 1) {
        digitalWrite(p, ((c->flags & 4) != 0) ^ ((c->flags & 2) != 0));
        pinMode(p, OUTPUT);
      } else {
        const unsigned bias = (c->flags >> 4) & 3;
        pinMode(p, bias == 2 ? INPUT_PULLUP : bias == 3 ? INPUT_PULLDOWN : INPUT);
      }
      break;
    case JW_PERIPHERAL_PWM:
#ifdef ESP32
#if ESP_ARDUINO_VERSION_MAJOR >= 3
      if (!ledcAttachChannel(p, c->value0, 12, i)) return JW_STATUS_ERROR;
#else
      if (ledcSetup(i, c->value0, 12) == 0) return JW_STATUS_ERROR;
      ledcAttachPin(p, i);
#endif
#else
      analogWriteResolution(12);
      analogWriteFrequency(p, c->value0);
#endif
      PwmWrite(i, 0);
      break;
    case JW_PERIPHERAL_SPI:
      digitalWrite(c->pins[3], HIGH);
      pinMode(c->pins[3], OUTPUT);
      if (!Other(i, c->kind, c->bus)) {
#ifdef ESP32
        Spi(c->bus)->begin(p, c->pins[2], c->pins[1], -1);
#else
        Spi(c->bus)->setSCK(p);
        Spi(c->bus)->setMOSI(c->pins[1]);
        Spi(c->bus)->setMISO(c->pins[2]);
        Spi(c->bus)->begin();
#endif
      }
      break;
    case JW_PERIPHERAL_I2C:
      if (!Other(i, c->kind, c->bus)) {
#ifdef ESP32
        if (!I2c(c->bus)->begin(p, c->pins[1], c->value0)) return JW_STATUS_ERROR;
        I2c(c->bus)->setTimeOut(c->timeout_ms);
#else
        I2c(c->bus)->setSDA(p);
        I2c(c->bus)->setSCL(c->pins[1]);
        I2c(c->bus)->begin();
        I2c(c->bus)->setClock(c->value0);
#endif
      }
      break;
    case JW_PERIPHERAL_UART:
#ifdef ESP32
      Uart(c->bus)->begin(c->value0, SERIAL_8N1, c->pins[1], p);
#else
      Uart(c->bus)->setTX(p);
      Uart(c->bus)->setRX(c->pins[1]);
      Uart(c->bus)->begin(c->value0);
#endif
      break;
    default:
      return JW_STATUS_INVALID_PAYLOAD;
  }
  return JW_STATUS_OK;
}
jw_status_t Release(void*, unsigned i) {
  auto& s = slots[i];
  if (!s.configured) return JW_STATUS_OK;
  const auto& c = s.config;
  if (c.kind == JW_PERIPHERAL_DIGITAL && (c.flags & 1))
    digitalWrite(c.pins[0], ((c.flags & 8) != 0) ^ ((c.flags & 2) != 0));
  if (c.kind == JW_PERIPHERAL_PWM) {
    PwmWrite(i, 0);
#ifdef ESP32
#if ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcDetach(c.pins[0]);
#else
    ledcDetachPin(c.pins[0]);
#endif
#endif
  }
  if (c.kind == JW_PERIPHERAL_UART) Uart(c.bus)->end();
  if (c.kind == JW_PERIPHERAL_SPI) {
    digitalWrite(c.pins[3], HIGH);
    pinMode(c.pins[3], INPUT);
    if (!Other(i, c.kind, c.bus)) {
      Spi(c.bus)->end();
      for (unsigned p = 0; p < 3; ++p) pinMode(c.pins[p], INPUT);
    }
  } else if (c.kind == JW_PERIPHERAL_I2C) {
    if (!Other(i, c.kind, c.bus)) {
      I2c(c.bus)->end();
      pinMode(c.pins[0], INPUT);
      pinMode(c.pins[1], INPUT);
    }
  } else {
    pinMode(c.pins[0], INPUT);
    if (c.kind == JW_PERIPHERAL_UART) pinMode(c.pins[1], INPUT);
  }
  s.configured = false;
  s.enabled = false;
  return JW_STATUS_OK;
}
jw_status_t Execute(
    void*, unsigned i, uint8_t op, const uint8_t* q, size_t n, uint8_t* out, size_t* size) {
  auto& s = slots[i];
  const auto& c = s.config;
  *size = 0;
  if (op == JW_PERIPHERAL_READ) {
    out[0] = (digitalRead(c.pins[0]) != LOW) ^ ((c.flags & 2) != 0);
    *size = 1;
  } else if (op == JW_PERIPHERAL_WRITE) {
    if (c.kind == JW_PERIPHERAL_PWM) {
      s.duty = static_cast<uint16_t>(q[0] | (uint16_t(q[1]) << 8));
      if (s.enabled) PwmWrite(i, s.duty);
    } else
      digitalWrite(c.pins[0], (q[0] != 0) ^ ((c.flags & 2) != 0));
    memcpy(out, q, n);
    *size = n;
  } else if (op == JW_PERIPHERAL_ENABLE || op == JW_PERIPHERAL_DISABLE) {
    s.enabled = op == JW_PERIPHERAL_ENABLE;
    PwmWrite(i, s.enabled ? s.duty : 0);
  } else if (op == JW_PERIPHERAL_TRANSFER) {
    const unsigned w = q[1], r = q[2];
    out[0] = 0;
    if (c.kind == JW_PERIPHERAL_SPI) {
      auto* bus = Spi(c.bus);
      const uint8_t modes[] = {SPI_MODE0, SPI_MODE1, SPI_MODE2, SPI_MODE3};
      bus->beginTransaction(SPISettings(c.value0, MSBFIRST, modes[c.value1]));
      digitalWrite(c.pins[3], LOW);
      for (unsigned j = 0; j < w; ++j) out[j + 1] = bus->transfer(q[j + 3]);
      digitalWrite(c.pins[3], HIGH);
      bus->endTransaction();
      out[0] = w;
    } else if (c.kind == JW_PERIPHERAL_I2C) {
      auto* bus = I2c(c.bus);
      if (w) {
        bus->beginTransmission(static_cast<uint8_t>(c.value1));
        if (bus->write(q + 3, w) != w || bus->endTransmission(r == 0) != 0) return JW_STATUS_ERROR;
      }
      if (r) {
        if (bus->requestFrom(static_cast<uint8_t>(c.value1),
                             static_cast<uint8_t>(r),
                             static_cast<uint8_t>(1)) != r)
          return JW_STATUS_ERROR;
        for (unsigned j = 0; j < r; ++j) {
          const int v = bus->read();
          if (v < 0) return JW_STATUS_ERROR;
          out[j + 1] = v;
        }
      }
      out[0] = r;
    } else {
      auto* port = Uart(c.bus);
      if (w) {
        if (port->availableForWrite() < static_cast<int>(w)) return JW_STATUS_BUSY;
        if (port->write(q + 3, w) != w) return JW_STATUS_ERROR;
      } else {
        while (out[0] < r && port->available() > 0) {
          const int v = port->read();
          if (v < 0) break;
          out[++out[0]] = static_cast<uint8_t>(v);
        }
      }
    }
    *size = out[0] + 1;
  } else
    return JW_STATUS_UNSUPPORTED;
  return JW_STATUS_OK;
}
int BusAllowed(void*) {
  for (uint8_t i = 0; i < g_num_channels; ++i)
    if (g_channels[i].enabled) return 0;
  return 1;
}
}  // namespace
int JoshuaArduinoPeripheralsInit() {
  memset(slots, 0, sizeof(slots));
  return JoshuaPeripheralDeviceInit({nullptr, Validate, Configure, Execute, Release, BusAllowed});
}
#endif
