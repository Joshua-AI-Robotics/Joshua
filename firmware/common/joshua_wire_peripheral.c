#include "joshua_wire_peripheral.h"

#include <string.h>
const uint8_t jw_peripheral_schema[JW_PERIPHERAL_SCHEMA_LEN] = {0x50, 1, 0, 0x3e, 32, 50};
static uint32_t get32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put32(uint8_t* p, uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(v >> (8 * i));
}
static int valid(const jw_peripheral_config_t* c) {
  unsigned pins;
  if (!c || c->kind < 1 || c->kind > 5) return 0;
  pins = c->kind == JW_PERIPHERAL_SPI                                    ? 4
         : c->kind == JW_PERIPHERAL_I2C || c->kind == JW_PERIPHERAL_UART ? 2
                                                                         : 1;
  for (unsigned i = 0; i < 4; ++i) {
    if ((i < pins) == (c->pins[i] == 255)) return 0;
    for (unsigned j = 0; j < i && i < pins; ++j)
      if (c->pins[i] == c->pins[j]) return 0;
  }
  if (c->kind != JW_PERIPHERAL_DIGITAL && c->flags) return 0;
  if (c->kind != JW_PERIPHERAL_I2C && c->timeout_ms) return 0;
  switch (c->kind) {
    case JW_PERIPHERAL_PWM:
      return !c->bus && c->value0 >= 1 && c->value0 <= 1000000 && c->value1 <= 65535;
    case JW_PERIPHERAL_SPI:
      return c->bus <= 1 && c->value0 >= 100000 && c->value0 <= 50000000 && c->value1 <= 3;
    case JW_PERIPHERAL_I2C:
      return c->bus <= 2 && c->value0 >= 10000 && c->value0 <= 400000 && c->value1 >= 8 &&
             c->value1 <= 119 && c->timeout_ms >= 1 && c->timeout_ms <= 50;
    case JW_PERIPHERAL_UART:
      return c->bus >= 1 && c->bus <= 8 && c->value0 >= 9600 && c->value0 <= 4000000 && !c->value1;
    case JW_PERIPHERAL_DIGITAL:
      return !c->bus && !c->value0 && !c->value1 && !(c->flags & 0xc0) &&
             ((c->flags & 1) || !(c->flags & 12));
  }
  return 0;
}
int jw_encode_peripheral_config(uint8_t* out, size_t cap, const jw_peripheral_config_t* c) {
  if (!out || cap < JW_PERIPHERAL_CONFIG_LEN || !valid(c)) return -1;
  out[0] = c->kind;
  out[1] = c->bus;
  memcpy(out + 2, c->pins, 4);
  put32(out + 6, c->value0);
  put32(out + 10, c->value1);
  out[14] = (uint8_t)c->timeout_ms;
  out[15] = (uint8_t)(c->timeout_ms >> 8);
  out[16] = c->flags;
  out[17] = 0;
  return JW_PERIPHERAL_CONFIG_LEN;
}
jw_result_t jw_decode_peripheral_config(const uint8_t* in, size_t n, jw_peripheral_config_t* c) {
  if (!in || !c || n != JW_PERIPHERAL_CONFIG_LEN || in[17]) return JW_RESULT_ERROR;
  c->kind = in[0];
  c->bus = in[1];
  memcpy(c->pins, in + 2, 4);
  c->value0 = get32(in + 6);
  c->value1 = get32(in + 10);
  c->timeout_ms = (uint16_t)(in[14] | ((uint16_t)in[15] << 8));
  c->flags = in[16];
  return valid(c) ? JW_RESULT_OK : JW_RESULT_ERROR;
}
int jw_peripheral_reply_valid(uint8_t op, const uint8_t* q, size_t n, const uint8_t* r, size_t m) {
  if ((n && !q) || (m && !r)) return 0;
  if (op == JW_PERIPHERAL_CONFIGURE) {
    jw_peripheral_config_t c;
    return jw_decode_peripheral_config(q, n, &c) == JW_RESULT_OK && m == n && !memcmp(q, r, n);
  }
  if (op == JW_PERIPHERAL_WRITE)
    return ((n == 1 && q[0] <= 1) || n == 2) && m == n && !memcmp(q, r, n);
  if (op == JW_PERIPHERAL_READ) return !n && m == 1 && r[0] <= 1;
  if (op == JW_PERIPHERAL_TRANSFER) {
    if (n < 3 || q[0] < JW_PERIPHERAL_SPI || q[0] > JW_PERIPHERAL_UART || n != 3u + q[1] || !m ||
        r[0] + 1u != m)
      return 0;
    if (q[0] == JW_PERIPHERAL_SPI) return q[1] >= 1 && q[1] <= 32 && q[2] == q[1] && r[0] == q[2];
    if (q[0] == JW_PERIPHERAL_I2C) return q[1] + q[2] >= 1 && q[1] + q[2] <= 32 && r[0] == q[2];
    return q[1] + q[2] >= 1 && q[1] + q[2] <= 32 && !(q[1] && q[2]) && r[0] <= q[2];
  }
  return op >= JW_PERIPHERAL_ENABLE && op <= JW_PERIPHERAL_RELEASE && !n && !m;
}

static int output_pin(unsigned board, unsigned pin) {
  if (board == 1) return pin <= 41;
  if (board != 2) return 0;
  switch (pin) {
    case 4:
    case 13:
    case 14:
    case 16:
    case 17:
    case 18:
    case 19:
    case 21:
    case 22:
    case 23:
    case 25:
    case 26:
    case 27:
    case 32:
    case 33:
      return 1;
  }
  return 0;
}
int jw_peripheral_route_valid(unsigned board, const jw_peripheral_config_t* c) {
  if (!valid(c)) return 0;
  if (c->kind == JW_PERIPHERAL_DIGITAL && !(c->flags & 1) && !(c->flags & 0x30)) return 0;
  const unsigned np = c->kind == 2 ? 4 : (c->kind == 3 || c->kind == 4 ? 2 : 1);
  for (unsigned i = 0; i < np; ++i) {
    if (output_pin(board, c->pins[i])) continue;
    if (board == 2 && c->kind == 5 && !(c->flags & 1) &&
        (c->pins[i] == 34 || c->pins[i] == 35 || c->pins[i] == 36 || c->pins[i] == 39) &&
        (c->flags & 0x30) <= 0x10)
      continue;
    return 0;
  }
  if (c->kind == JW_PERIPHERAL_DIGITAL && (c->flags & 1) && (c->flags & 0x30)) return 0;
  if (c->kind == JW_PERIPHERAL_I2C &&
      (board == 1 ? (c->timeout_ms != 50 || (c->value0 != 100000 && c->value0 != 400000))
                  : c->timeout_ms > 5))
    return 0;
  if (board == 2) return c->kind != 3 ? (c->kind != 4 || c->bus <= 2) : c->bus <= 1;
  if (c->kind == JW_PERIPHERAL_PWM) {
    const uint8_t pwm[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13,
                           14, 15, 18, 19, 22, 23, 24, 25, 28, 29, 33, 36, 37};
    for (unsigned i = 0; i < sizeof(pwm); ++i)
      if (pwm[i] == c->pins[0]) return 1;
    return 0;
  }
  if (c->kind == JW_PERIPHERAL_SPI)
    return c->bus == 0 ? c->pins[0] == 13 && c->pins[1] == 11 && c->pins[2] == 12
                       : c->bus == 1 && c->pins[0] == 27 && c->pins[1] == 26 &&
                             (c->pins[2] == 1 || c->pins[2] == 39);
  if (c->kind == JW_PERIPHERAL_I2C) {
    const uint8_t sda[] = {18, 17, 25}, scl[] = {19, 16, 24};
    return c->pins[0] == sda[c->bus] && c->pins[1] == scl[c->bus];
  }
  if (c->kind == JW_PERIPHERAL_UART) {
    const uint8_t tx[] = {1, 8, 14, 17, 20, 24, 29, 35}, rx[] = {0, 7, 15, 16, 21, 25, 28, 34};
    return c->pins[0] == tx[c->bus - 1] && c->pins[1] == rx[c->bus - 1];
  }
  // Teensy supports pull-up and pull-down; output bias requests are rejected by
  // the native adapter rather than silently changing drive semantics.
  return c->kind == JW_PERIPHERAL_DIGITAL;
}
