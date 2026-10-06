// Declares shared STEP/DIR command/reset callbacks and board identity/ESTOP
// state for Teensy and ESP32. JoshuaWire reuses these command semantics
// through joshua_wire_serial_endpoint; GPIO control stays in backend_stepdir.
#pragma once

#include <stdbool.h>

#include "joshua_wire_commands.h"

typedef struct {
  jw_board_id_t board_id;
  const char* firmware_name;
  bool estopped;
} JoshuaStepDirProtocol;

int JoshuaStepDirCommand(void* context,
                         const jw_command_t* command,
                         uint8_t* response,
                         size_t capacity);
void JoshuaStepDirReset(void* context);
