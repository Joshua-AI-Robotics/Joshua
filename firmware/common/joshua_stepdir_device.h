// Shared revision-4 firmware application for Arduino STEP/DIR boards.
#pragma once

#include "joshua_wire_endpoint.h"
#include "stepdir_channel.h"

// Call only from the image's single dispatch/service loop. Hardware acquisition
// runs in Service, and command handlers return cached feedback without blocking.
void StepDirDeviceInit(uint32_t boot_id, const char* name, bool (*valid_output_pin)(uint8_t));
void StepDirDeviceService();
void StepDirDeviceFault();
int StepDirDeviceProcess(const uint8_t* request, size_t size, uint8_t* response, size_t capacity);

// Optional setup-time composition. Storage is supplied by the image and lives
// for the MCU lifetime. Only called before exposing transport/configured outputs.
struct StepDirDeviceExtension {
  void* context;
  void (*safety)(void*, int);
  void (*service)(void*, uint64_t);
  // -2 means not handled; otherwise a normal common reply length/-1.
  int (*command)(void*, const jw_command_t*, uint8_t*, size_t);
  void (*observed)(void*, const jw_command_t*, const uint8_t*, size_t);
};
int StepDirDeviceExtend(jw_runtime_function_t* functions,
                        size_t function_capacity,
                        jw_resource_claim_t* resources,
                        size_t resource_capacity,
                        StepDirDeviceExtension extension);
jw_device_runtime_t* StepDirDeviceRuntime();
uint64_t StepDirDeviceNowUs();
