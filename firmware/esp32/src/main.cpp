// Serial firmware artifact: JOSHUA_WIRE_VERSION selects v1 or v2 explicitly.
#include <Arduino.h>

#include "backend_stepdir.h"
#include "channel_table.h"
#include "joshua_stepdir_commands.h"
#include "joshua_wire_serial_endpoint.h"
#include "transport_serial.h"

namespace {
jw_serial_endpoint_t endpoint;
JoshuaStepDirProtocol protocol{JW_BOARD_ESP32,
                               JOSHUA_WIRE_VERSION == 2 ? "esp32-serial-v2" : nullptr,
                               JOSHUA_WIRE_VERSION == 2,
                               false};
}  // namespace

void setup() {
  TransportInit();
  for (uint8_t i = 0; i < g_num_channels; ++i) StepDirInit(&g_channels[i]);
  jw_serial_endpoint_init(&endpoint, JOSHUA_WIRE_VERSION);
}

void loop() {
  uint8_t request[JW2_MAX_FRAME_LEN];
  uint8_t response[JW2_MAX_FRAME_LEN];
  const size_t request_len = TransportReadFrame(request, sizeof(request));
  if (request_len != 0) {
    const int len = jw_serial_endpoint_process(&endpoint,
                                               request,
                                               request_len,
                                               response,
                                               sizeof(response),
                                               JoshuaStepDirCommand,
                                               JoshuaStepDirReset,
                                               &protocol);
    if (len > 0) TransportWriteFrame(response, static_cast<size_t>(len));
  }
  for (uint8_t i = 0; i < g_num_channels; ++i) StepDirService(&g_channels[i]);
}
