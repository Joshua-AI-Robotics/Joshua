// Peripheral profile v1.0. Independent of the scalar I/O profile and core framing.
#pragma once
#include "joshua_wire.h"
#ifdef __cplusplus
extern "C" {
#endif
#define JW_PROFILE_PERIPHERAL 4
#define JW_PERIPHERAL_CONFIGURE 0
#define JW_PERIPHERAL_TRANSFER 1
#define JW_PERIPHERAL_READ 2
#define JW_PERIPHERAL_WRITE 3
#define JW_PERIPHERAL_ENABLE 4
#define JW_PERIPHERAL_DISABLE 5
#define JW_PERIPHERAL_RELEASE 6
#define JW_PERIPHERAL_CONFIG_LEN 18
#define JW_PERIPHERAL_SCHEMA_LEN 6
#define JW_PERIPHERAL_MAX_TRANSFER 32
#define JW_PERIPHERAL_FIRST_ID 192
#define JW_PERIPHERAL_SLOTS 8
#define JW_PERIPHERAL_PWM 1
#define JW_PERIPHERAL_SPI 2
#define JW_PERIPHERAL_I2C 3
#define JW_PERIPHERAL_UART 4
#define JW_PERIPHERAL_DIGITAL 5
// Digital flags: output, active-low, initial, shutdown; bias bits4..5:
// 0=as-is, 1=disabled, 2=pull-up, 3=pull-down. Unused high bits must be zero.
typedef struct {
  uint8_t kind, bus, pins[4];  // Unused pins are 255; native firmware IDs.
  uint32_t value0, value1;     // Hz/baud; PWM duty16, SPI mode, or I2C address.
  uint16_t timeout_ms;
  uint8_t flags;
} jw_peripheral_config_t;
int jw_encode_peripheral_config(uint8_t*, size_t, const jw_peripheral_config_t*);
jw_result_t jw_decode_peripheral_config(const uint8_t*, size_t, jw_peripheral_config_t*);
// Exact version/limits signature, used by discovery before any configuration.
extern const uint8_t jw_peripheral_schema[JW_PERIPHERAL_SCHEMA_LEN];
// Semantic success validation inside the correlated session, before admitting
// another mutation. Invalid acknowledgments retain exact-retry uncertainty.
int jw_peripheral_reply_valid(uint8_t operation,
                              const uint8_t* request,
                              size_t request_len,
                              const uint8_t* reply,
                              size_t reply_len);
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
extern "C" {
#endif
// Exact supported connector routes, shared by offline validation and firmware.
// board=1 Teensy 4.1 outer headers, board=2 ESP32 DevKitC V4 / WROOM-32.
int jw_peripheral_route_valid(unsigned board, const jw_peripheral_config_t*);
#ifdef __cplusplus
}
#endif
