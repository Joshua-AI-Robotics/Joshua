// Serial firmware artifact using JoshuaWire 0.0.2.
#include <Arduino.h>

#include "backend_stepdir.h"
#include "channel_table.h"
#include "joshua_stepdir_commands.h"
#include "joshua_wire_endpoint.h"
#include "transport_serial.h"

#if !defined(JOSHUA_TRANSPORT_SERIAL)
#error "Select an explicit serial firmware build profile"
#endif

namespace {
jw_endpoint_t endpoint;
SerialTransport serial;
frame_transport_t transport;
uint8_t response[JW_MAX_FRAME_LEN];
size_t pending_response_length;
JoshuaStepDirProtocol protocol{JW_BOARD_TEENSY41, "teensy-serial", false};
}  // namespace

void setup() {
  transport = TransportSerialInit(&serial, {115200, 20});
  for (uint8_t i = 0; i < g_num_channels; ++i) StepDirInit(&g_channels[i]);
  jw_endpoint_init(&endpoint);
  pending_response_length = 0;
}

void loop() {
  // Do not consume another request or execute this command again while its
  // response is awaiting acceptance. The endpoint itself performs no I/O.
  if (pending_response_length == 0) {
    uint8_t request[JW_MAX_FRAME_LEN];
    size_t request_length = 0;
    if (transport.poll_receive(transport.context, request, sizeof(request), &request_length) ==
        FRAME_OK) {
      const int length = jw_endpoint_process(&endpoint,
                                             request,
                                             request_length,
                                             response,
                                             sizeof(response),
                                             JoshuaStepDirCommand,
                                             JoshuaStepDirReset,
                                             &protocol);
      if (length > 0) pending_response_length = static_cast<size_t>(length);
    }
  }
  if (pending_response_length != 0 &&
      transport.try_send(transport.context, response, pending_response_length) == FRAME_OK) {
    pending_response_length = 0;
  }
  for (uint8_t i = 0; i < g_num_channels; ++i) StepDirService(&g_channels[i]);
}
