#include <Arduino.h>
#include <esp_system.h>

#include "joshua_stepdir_device.h"
#ifdef JOSHUA_PERIPHERAL_REFERENCE
#include "joshua_peripheral_device.h"
#endif
#include "transport_serial.h"
namespace {
#ifdef JOSHUA_PERIPHERAL_REFERENCE
bool peripheral_ready;
#endif
SerialTransport serial;
frame_transport_t transport;
uint8_t response[JW_MAX_FRAME_LEN];
size_t pending_response_length;

void TransmitTimeout(void*) {
  StepDirDeviceFault();
}

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
  pending_response_length = 0;
}
void loop() {
#ifdef JOSHUA_PERIPHERAL_REFERENCE
  if (!peripheral_ready) return;
#endif
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
}
