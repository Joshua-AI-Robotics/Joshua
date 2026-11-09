// Reference mixed STEP/DIR + intrinsic scalar ADC0 function. MCU bindings own
// register access. No firmware flashing or native hardware access occurs here.
#pragma once
#include "joshua_scalar_sensor.h"
#include "joshua_stepdir_device.h"
#include "joshua_watchdogs.h"

#define JW_REFERENCE_ADC_FUNCTION 128u
#define JW_REFERENCE_ADC_MIN_PERIOD_US 1000u
#define JW_REFERENCE_ADC_MAX_PERIOD_US 1000000u
#define JW_REFERENCE_ADC_TIMEOUT_US 1000u
#define JW_REFERENCE_ADC_MAX_AGE_US 100000u
struct JoshuaMixedDevice {
  jw_runtime_function_t functions[JW_WATCHDOG_MAX_ACTUATORS + 1];
  jw_resource_claim_t resources[3 * JW_WATCHDOG_MAX_ACTUATORS + 2];
  jw_scalar_sensor_t sensor;
  jw_watchdogs_t watchdogs;
};
// Called after StepDirDeviceInit and before exposing transport. A0 is an
// intrinsic address of the reference image; robot config selects function 128.
int JoshuaMixedDeviceInit(JoshuaMixedDevice*, uint16_t native_analog_pin, jw_scalar_acquisition_t);
int JoshuaMixedDeviceSample(void*, uint8_t*, size_t, uint32_t*);
void JoshuaMixedDeviceSubmitted(void*, uint32_t);
