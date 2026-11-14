#include <Arduino.h>
#include <Entropy.h>

#include "joshua_stepdir_device.h"
#ifdef JOSHUA_PERIPHERAL_REFERENCE
#include "joshua_peripheral_device.h"
#endif
#include "transport_serial.h"
#ifdef JOSHUA_MIXED_ADC_REFERENCE
#include "adc_reference.h"
#include "joshua_mixed_device.h"
#include "joshua_mixed_serial.h"
#endif
namespace {
#ifdef JOSHUA_PERIPHERAL_REFERENCE
bool peripheral_ready;
#endif
SerialTransport serial;
frame_transport_t transport;
#ifndef JOSHUA_MIXED_ADC_REFERENCE
uint8_t response[JW_MAX_FRAME_LEN];
size_t pending_response_length;
#endif
#ifdef JOSHUA_MIXED_ADC_REFERENCE
JoshuaMixedDevice mixed;
jw_mixed_serial_t scheduler;
bool mixed_ready;
#endif

void TransmitTimeout(void*) {
  StepDirDeviceFault();
}

bool ValidOutputPin(uint8_t pin) {
  return pin < CORE_NUM_DIGITAL;
}
uint32_t BootId() {
  Entropy.Initialize();
  return Entropy.random();
}
}  // namespace

void setup() {
  StepDirDeviceInit(BootId(), "teensy-stepdir", ValidOutputPin);
#ifdef JOSHUA_PERIPHERAL_REFERENCE
  peripheral_ready = JoshuaArduinoPeripheralsInit();
  if (!peripheral_ready) {
    StepDirDeviceFault();
    return;
  }
#endif
  transport = TransportSerialInit(&serial, {115200, 20});
  serial_frame_transport_set_tx_timeout(
      &serial, JW_SERIAL_TX_STOP_TIMEOUT_MS, TransmitTimeout, nullptr);
#ifndef JOSHUA_MIXED_ADC_REFERENCE
  pending_response_length = 0;
#endif
#ifdef JOSHUA_MIXED_ADC_REFERENCE
  mixed_ready = JoshuaMixedDeviceInit(&mixed, A0, ReferenceAnalogAcquisition());
  const jw_mixed_serial_ops_t ops{&mixed,
                                  [](void*) { StepDirDeviceService(); },
                                  [](void*, const uint8_t* in, size_t n, uint8_t* out, size_t cap) {
                                    return StepDirDeviceProcess(in, n, out, cap);
                                  },
                                  [](void*) { StepDirDeviceFault(); },
                                  JoshuaMixedDeviceSample,
                                  JoshuaMixedDeviceSubmitted};
  jw_mixed_serial_init(&scheduler, transport, ops);
#endif
}
void loop() {
#ifdef JOSHUA_PERIPHERAL_REFERENCE
  if (!peripheral_ready) return;
#endif
#ifdef JOSHUA_MIXED_ADC_REFERENCE
  if (mixed_ready) jw_mixed_serial_service(&scheduler);
#else
  StepDirDeviceService();
  if (pending_response_length == 0) {
    uint8_t request[JW_MAX_FRAME_LEN];
    size_t size = 0;
    if (transport.poll_receive(transport.context, request, sizeof(request), &size) == FRAME_OK) {
      const int result = StepDirDeviceProcess(request, size, response, sizeof(response));
      if (result > 0) pending_response_length = static_cast<size_t>(result);
    }
  }
  if (pending_response_length &&
      transport.try_send(transport.context, response, pending_response_length) == FRAME_OK)
    pending_response_length = 0;
#endif
}
