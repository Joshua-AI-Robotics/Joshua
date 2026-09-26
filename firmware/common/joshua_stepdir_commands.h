// Declares shared STEP/DIR command/reset callbacks and board identity/ESTOP
// state for Teensy and ESP32. Both wire versions reuse these command semantics
// through joshua_wire_serial_endpoint; GPIO control stays in backend_stepdir.
#pragma once

#include "joshua_wire_serial_endpoint.h"

typedef struct {
  jw_board_id_t board_id;
  const char* firmware_name;
  // A v2 ESTOP stays latched until a new session. Legacy v1 behavior is
  // retained by setting latch_estop to false in that artifact.
  bool latch_estop;
  bool estopped;
} JoshuaStepDirProtocol;

int JoshuaStepDirCommand(void* context,
                         const jw1_frame_t* command,
                         uint8_t* response,
                         size_t capacity);
void JoshuaStepDirReset(void* context);
