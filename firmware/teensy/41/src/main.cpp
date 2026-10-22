#include <Arduino.h>
#include <Entropy.h>

#include "joshua_stepdir_device.h"
#include "transport_serial.h"
namespace {
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
  TransportInit();
}
void loop() {
  StepDirDeviceService();
  uint8_t request[JW_MAX_FRAME_LEN], response[JW_MAX_FRAME_LEN];
  const size_t size = TransportReadFrame(request, sizeof(request));
  if (size) {
    const int result = StepDirDeviceProcess(request, size, response, sizeof(response));
    if (result > 0) TransportWriteFrame(response, result);
  }
}
