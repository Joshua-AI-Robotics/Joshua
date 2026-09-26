// Declares AM243's software-only channel state and command/reset callbacks for
// the shared serial endpoint. UART I/O stays in joshua_serial_task.c; command
// semantics can therefore be tested without the TI SDK or physical hardware.
// TODO(JoshuaWire v2 EtherCAT migration): Replace this serial-specific handler
// with a transport-independent firmware dispatcher shared by UART, CoE mailbox
// management commands and PDO target/feedback commands. Unify channel state,
// reset/ESTOP safety rules and command arbitration across those entry points;
// keep UART I/O in joshua_serial_task.c. ROS 2 host support does not replace
// firmware command execution. See docs/BOARD_COMM_SEPARATION_PLAN.md sections 5-9.
#pragma once

#include <stdbool.h>

#include "joshua_wire_serial_endpoint.h"

typedef struct {
  bool configured;
  bool enabled;
  bool estopped;
  bool latch_estop;
  jw_configure_step_dir_t config;
  jw_mode_t target_mode;
  float target_value;
} JoshuaSerialChannel;

int JoshuaSerialCommand(void* context,
                        const jw1_frame_t* frame,
                        uint8_t* response,
                        size_t capacity);
void JoshuaSerialReset(void* context);
