// Native GPIO/clock seam for compiling the production Arduino firmware backend.
#pragma once
#include <stdint.h>
#define HIGH 1
#define LOW 0
#define OUTPUT 1
unsigned long micros();
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
