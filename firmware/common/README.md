# Shared firmware and JoshuaWire

Shared command definitions and codecs connect the [host board layer](../../robot/README.md)
to Joshua's [MCU firmware](../README.md). Board firmware also uses the command
dispatcher and STEP/DIR backend here. Start with `joshua_wire_commands.h`, then
the codec for the wire version you are working on.

The STEP/DIR backend can move real motors when compiled into firmware. See the
[hardware-safety rules](../../AGENTS.md#hardware-safety--read-this-first).

## Contents

- [joshua_wire_commands.h](joshua_wire_commands.h) — version-neutral command
  IDs, statuses, modes and payload types. `JW_CMD_*` describes an operation,
  not a wire version; RESET_SESSION is supported only by v2 endpoints.
- [joshua_wire_v1.h](joshua_wire_v1.h) / [joshua_wire_v2.h](joshua_wire_v2.h)
  and their `.c` files — stateless frame codecs. V2 adds session/message IDs.
  Functions named `jw1_encode_*` still produce v1 frames, even when their
  arguments use the shared `jw_*_t` types.
- [joshua_wire_v2_firmware_session.h](joshua_wire_v2_firmware_session.h)
  and `.c` — firmware-side reset, request history and duplicate suppression.
  This is state used by a dispatch loop, not a thread or network service.
  Its host-side counterpart is
  [JoshuaWireV2Session](../../robot/board/joshua_wire/joshua_wire_v2_session.h).
- [joshua_wire_serial_endpoint.h](joshua_wire_serial_endpoint.h) and `.c` —
  connects an explicitly selected v1/v2 artifact to firmware command handlers.
  It receives complete frames; the board-specific UART/USB code owns I/O.
- [joshua_stepdir_commands.h](joshua_stepdir_commands.h) and `.cpp` — shared
  Teensy/ESP32 command handling, calling [backend_stepdir.h](backend_stepdir.h)
  and `.cpp` for physical pin control. Each board supplies `channel_table.h`.

For a v2 serial request, follow the board's receive loop into the serial
endpoint, then the firmware session, then the command handler and drive backend.
The response carries the same session/message IDs back to the host.

## Responsibilities and boundaries

Codecs validate bytes; sessions track request lifetimes; command handlers apply
operations; drive backends control hardware. The same C codec sources build on
host and MCU. UART drivers stay in board directories, while host link ownership
and I/O deadlines belong to [robot/comm](../../robot/comm/README.md).

This directory does not yet implement JoshuaWire EtherCAT PDO/CoE endpoints,
cross-transport arbitration or communication-loss watchdogs. The
[separation plan](../../docs/BOARD_COMM_SEPARATION_PLAN.md) tracks that work.

## Temporary migration adapters

These are implementation bridges, not the intended final architecture:

- **Firmware serial endpoint:** handlers still accept a `jw1_frame_t` view and
  return a v1-encoded reply in memory. The endpoint extracts its payload and
  emits the selected wire version. Replace this conversion when handlers and
  payload encoders accept version-neutral command/payload views; keep explicit
  artifact selection and v1/v2 rejection tests. V2 wire bytes never enter the
  v1 decoder.
- **Host `JoshuaWireV2Session`:** wraps the existing board engine's in-memory
  v1 calls. Remove that wrapper interface when the composed board engine uses
  neutral payload helpers and `MessageTransport::Exchange` directly. Preserve
  session reset, correlation, ID exhaustion and teardown behavior.
- **AM243 [serial command handler](../am243/joshua_dual_transport_v1/src/joshua_serial_commands.h):**
  replace the software-only, serial-specific dispatcher when UART, CoE and PDO
  share coordinated channel state and safety handling. Physical motion also
  requires a real motor backend; ROS 2 host support alone does not replace it.

## Tests and builds

`*_test.cc` files are maintained source, kept beside the code they verify:
commands tests pin wire values, v1 tests cover legacy framing, and v2 tests
cover framing, firmware sessions and the serial endpoint. Board-specific native
tests also compile the real Teensy/ESP32 dispatch with the test-only
[Arduino substitute](../testing/Arduino.h) and
[shared test suite](../testing/serial_firmware_test.cc). Those helpers use no
physical I/O and do not validate pulse timing.

[BUILD](BUILD) defines host/native targets; [library.json](library.json) exposes
MCU sources to PlatformIO and excludes `*_test.cc`. AM243's Makefile lists its
C sources explicitly. See [CONTRIBUTING](../../CONTRIBUTING.md#testing-and-ci)
for the Docker test workflow and [firmware build instructions](../README.md)
for MCU artifacts. Build outputs and caches are not source files to commit.
