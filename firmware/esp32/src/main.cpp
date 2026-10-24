#include <Arduino.h>
#include <esp_system.h>

#include "joshua_stepdir_device.h"
#include "transport_serial.h"
namespace {
bool ValidOutputPin(uint8_t pin) {
  // UART0 carries JoshuaWire; flash and input-only pins cannot drive channels.
  return digitalPinCanOutput(pin) && pin != 1 && pin != 3 && !(pin >= 6 && pin <= 11);
}
uint32_t BootId() {
  return esp_random();
}
}  // namespace

void setup() {
  StepDirDeviceInit(BootId(), "esp32-stepdir", ValidOutputPin);
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
