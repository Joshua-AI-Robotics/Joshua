// Incremental JoshuaWire serial receive; never waits for a partial frame.
#pragma once
#include <stddef.h>
#include <stdint.h>
void TransportInit();
size_t TransportReadFrame(uint8_t* frame, size_t capacity);
void TransportWriteFrame(const uint8_t* frame, size_t size);
