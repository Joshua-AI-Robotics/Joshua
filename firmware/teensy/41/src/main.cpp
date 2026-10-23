#include <Arduino.h>
#include <Entropy.h>

#include "joshua_stepdir_device.h"
#include "transport_serial.h"
namespace {
SerialTransport serial;
frame_transport_t transport;
uint8_t response[JW_MAX_FRAME_LEN];
size_t pending_response_length;

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
  transport = TransportSerialInit(&serial, {115200, 20});
  pending_response_length = 0;
}
void loop() {
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
