#ifdef JOSHUA_MIXED_ADC_REFERENCE
#include "adc_reference.h"

#include <Arduino.h>

namespace {
// Teensy 4.1 intrinsic A0 address: pad AD_B1_02, ADC1 channel 7. Core startup
// initializes/calibrates ADC1. These runtime hooks never call analogRead or wait
// for calibration/conversion. Reference: PJRC cores/teensy4/analog.c.
int Start(void*) {
  if ((ADC1_GC & ADC_GC_CAL) || (ADC1_GS & ADC_GS_CALF)) return 0;
  pinMode(A0, INPUT_DISABLE);
  ADC1_CFG = (ADC1_CFG & ~ADC_CFG_MODE(3)) | ADC_CFG_MODE(1);  // Native 10-bit codes.
  ADC1_GC &= ~ADC_GC_ADCO;
  ADC1_HC0 = 7;
  return 1;
}
int Poll(void*, float* value) {
  if (ADC1_GS & ADC_GS_CALF) return -1;
  if (!(ADC1_HS & ADC_HS_COCO0)) return 0;
  *value = static_cast<float>(ADC1_R0);
  return 1;
}
void Cancel(void*) {
  ADC1_HC0 = 31;
}
}  // namespace
jw_scalar_acquisition_t ReferenceAnalogAcquisition() {
  return {nullptr, Start, Poll, Cancel};
}
#endif
