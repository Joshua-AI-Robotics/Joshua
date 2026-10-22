// Shared revision-4 firmware application for Arduino STEP/DIR boards.
#pragma once

#include "joshua_wire_endpoint.h"
#include "stepdir_channel.h"

// Call only from the image's single dispatch/service loop. Hardware acquisition
// runs in Service, and command handlers return cached feedback without blocking.
void StepDirDeviceInit(uint32_t boot_id, const char* name, bool (*valid_output_pin)(uint8_t));
void StepDirDeviceService();
int StepDirDeviceProcess(const uint8_t* request, size_t size, uint8_t* response, size_t capacity);
