# Shared firmware and JoshuaWire

Shared command definitions and codecs connect the [host board layer](../../robot/README.md)
to Joshua's [MCU firmware](../README.md). Board firmware also uses the command
dispatcher and STEP/DIR backend here. JoshuaWire (JW) is version `0.0.2` and is the sole Joshua protocol.
Start with `joshua_wire_commands.h`, then `joshua_wire.h`. The on-wire
revision remains `2`; the release version does not change validated frame bytes.

The STEP/DIR backend can move real motors when compiled into firmware. See the
[hardware-safety rules](../../AGENTS.md#hardware-safety--read-this-first).

## Contents

- [joshua_wire_commands.h](joshua_wire_commands.h) and `.c` — version-neutral
  command views (`jw_command_t`), IDs, semantic types and payload-only codecs.
  `JW_CMD_*` describes an operation, not a wire version; RESET_SESSION is
  required before normal commands. Payload codecs never add headers, IDs or CRCs.
- [joshua_wire.h](joshua_wire.h) and `.c` — stateless correlated frame codec,
  `jw_*` functions and `JW_*` limits/version constants. The JW1 codec is removed.
- [joshua_wire_firmware_session.h](joshua_wire_firmware_session.h)
  and `.c` — firmware-side reset, request history and duplicate suppression.
  This is state used by a dispatch loop, not a thread or network service.
  Its host-side counterpart is
  [JoshuaWireSession](../../robot/board/joshua_wire/joshua_wire_session.h).
- [joshua_wire_endpoint.h](joshua_wire_endpoint.h) and `.c` —
  connects the JW session to firmware command handlers.
  It takes one complete request and returns one complete response; it never
  opens devices, waits for traffic or transmits bytes.
- [frame_transport.h](frame_transport.h) — C-compatible, nonblocking frame I/O
  with an implementation context. One instance represents one peer/controller.
  Receive returns one complete JW frame, `FRAME_NO_DATA`, or an error. Send
  accepts the entire frame, or accepts none and returns `FRAME_WOULD_BLOCK`/an
  error. Accepted bytes are copied or consumed before return; acceptance does
  not establish delivery or execution of the remote command.
- [serial_frame_assembler.h](serial_frame_assembler.h) and `.c` — incremental
  UART/USB link framing used by Teensy, ESP32 and AM243. It retains partial bytes
  and expires stalled input using the adapter's inter-byte timeout. CRC checks,
  session IDs, duplicate handling and command dispatch stay in the shared JW
  codec/session/endpoint. Board bindings supply the clock and nonblocking I/O.
- [serial_frame_transport.h](serial_frame_transport.h) and `.c` — shared serial
  adapter for Teensy/ESP32 byte bindings. It copies an accepted response into
  one TX slot and advances short driver writes without interleaving frames.
  A busy slot returns `FRAME_WOULD_BLOCK` for the next response. Polls and send
  attempts advance accepted data without waiting for bytes or output space.
- [joshua_wire_ethercat.h](joshua_wire_ethercat.h) — shared layout-v1 PDO/CoE
  sizes, offsets, object indices and transport bits. The host adapters use this
  contract for every board; this header itself implements no
  endpoint or watchdog.
- [joshua_ethercat_profile.h](joshua_ethercat_profile.h) / `.c` — board- and
  slave-stack-independent firmware endpoint: descriptor, CoE/PDO correlation,
  shared session, retained replies, fault latch and per-channel target watchdogs.
  Identity and command/reset/stop/enabled callbacks come from the board.
- [joshua_stepdir_commands.h](joshua_stepdir_commands.h) and `.cpp` — shared
  Teensy/ESP32 command handling, calling [backend_stepdir.h](backend_stepdir.h)
  and `.cpp` for physical pin control. Each board supplies `channel_table.h`.

For a JW serial request, follow the board's receive loop into the shared
endpoint, then the firmware session, then the command handler and drive backend.
The response carries the same session/message IDs back to the host.

## Responsibilities and boundaries

Codecs validate bytes; sessions track request lifetimes; command handlers apply
operations; drive backends control hardware. The same C codec sources build on
host and MCU. UART drivers stay in board directories, while host link ownership
and I/O deadlines belong to [robot/comm](../../robot/comm/README.md).

Startup initializes the selected adapter, the shared JW endpoint, and board
command/reset callbacks. Hardware settings stay in the adapter configuration.
Each loop polls a complete request, processes it once, and tries to send the
response. It retains that response until the adapter accepts it, and consumes
no further request while waiting. Retrying a send never executes the command
again. Motor channels are serviced on every iteration, including partial input
and blocked output; serial noise draining is bounded to 64 bytes per iteration.
An unrecoverable link error requires board-specific recovery; the pending
response is retained rather than silently discarded.

The `teensy41-serial` and `esp32-serial` PlatformIO profiles explicitly select
their UART/USB adapter. AM243's `ti-demo` profile selects its UART adapter in a
dedicated RTOS task with a software-only channel. The `jw` profile
selects the existing EtherCAT capability instead: complete process-data
snapshots, management messages, deadlines and watchdogs remain in that profile.
EtherCAT reuses the JW codec/session/commands without pretending its cyclic
timing and delivery guarantees are those of `frame_transport_t`.

The [AM243 overlay](../am243/joshua_dual_transport/README.md#opt-in-jw-ethercat-profile)
adapts this shared EtherCAT profile to its TI stack and software-only channel.
Physical-output safety and cross-transport arbitration remain unfinished. The
[separation plan](../../docs/BOARD_COMM_SEPARATION_PLAN.md) tracks that work.

## Porting JW EtherCAT to another board

The host checks the JWEC protocol/layout/capabilities, not a vendor/product ID
or artifact name. A new conforming board does not require a new comm adapter.
SOES, Beckhoff SSC, or another slave stack is a firmware-side choice; none is
part of the host wire contract. Board identity checks belong to the host board
layer, separately from EtherCAT transport. A new model may still need identity
registration there; existing model configurations use the same comm path.

1. Reuse the codecs, session and `joshua_ethercat_profile` from this directory.
   Supply a `JoshuaEthercatProfileConfig` with the board's IDENTIFY metadata,
   printable descriptor artifact label, and synchronous drive callbacks. The
   profile copies this config; callback context must remain alive.
2. Implement command payload handling and actual drive I/O. `reset` must stop
   all outputs and invalidate configuration before returning. `stop` must stop
   every channel, clear targets, latch faults for feedback, and tolerate repeat
   calls. `enabled` reports actual per-channel state. The profile never toggles
   pins or assumes a software-feedback channel. Fresh targets on one channel
   cannot keep another enabled channel alive; a watchdog fault stops the board.
3. In the board/stack adapter, register the objects in `joshua_wire_ethercat.h`:
   descriptor/reset/request/response/ack at subindex zero, no Complete Access,
   with their exact sizes and access directions. Map complete 80-byte output
   and input PDO images. Match the ESI/SII identity and mapping to that firmware.
4. Serialize every profile call and callback; provide complete PDO snapshots
   and monotonic microseconds. Call `Tick` periodically independently of command
   arrival and on OP loss. Callbacks must be bounded, nonblocking and must not
   re-enter the profile. Publish reset readback only after reset completes.
5. Configure the host's slave index, paired JW transport and explicit timing
   budgets. Layout-v1 currently requires split LRD/LWR support; other layouts or
   capabilities require a versioned generic extension, never board-name checks.
6. Validate identity, reset, both command planes, duplicates, per-channel
   freshness, OP/link loss and stop behavior on the new hardware. Native tests
   cannot establish physical-output safety or production timing.

## Command and frame boundaries

Firmware handlers consume `jw_command_t` and return payload bytes. The shared
endpoint and firmware session supply framing, correlation and retry handling.
Host channels use `JoshuaWireSession`, which exchanges JW frames directly
through comm and returns validated payloads. Vendor protocols retain their
separate fixed-size transport API; JoshuaWire does not use it.

## Remaining migration work

- Host message/cyclic board-engine composition and factory assembly now exist.
  Framed-serial extraction and configurable serial exchange/settle timing are
  implemented. Serial and EtherCAT both use the same correlated JW protocol.
- **AM243 [command handler](../am243/joshua_dual_transport/src/joshua_commands.h):**
  now transport-neutral and reused by separate UART and EtherCAT artifacts.
  Simultaneous UART/CoE/PDO ownership still needs an arbiter; physical motion
  needs a real motor backend. ROS 2 host support alone supplies neither.

## Tests and builds

For manual serial checks after an intentional flash, use the
[JW validation guide](../../docs/JOSHUA_WIRE_VALIDATION.md). The shared probe
defaults to reset/identify/ESTOP only; the old JW1 smoke tool is removed.

`*_test.cc` files are maintained source, kept beside the code they verify:
commands tests pin wire values, payload bytes and bounds; JW tests cover golden
frames, legacy-frame rejection, firmware sessions and the shared endpoint.
Assembler tests cover split frames, resynchronization, timeout clock wrap and
separation of framing from CRC validation. Board-specific native
tests also compile the real Teensy/ESP32 dispatch with the test-only
[Arduino substitute](../testing/Arduino.h) and
[shared test suite](../testing/serial_firmware_test.cc). Those helpers use no
physical I/O and do not validate pulse timing.
They also check continued motor service during partial input and blocked TX,
whole-frame send acceptance, bounded noise draining and retention of a response
without repeating command dispatch.
AM243's native UART adapter test substitutes only the TI FIFO/clock calls;
MCU builds use the real SDK. These checks cannot qualify FIFO timing on hardware.

[BUILD](BUILD) defines host/native targets; [library.json](library.json) exposes
MCU sources to PlatformIO and excludes `*_test.cc`. AM243's Makefile lists its
C sources explicitly. See [CONTRIBUTING](../../CONTRIBUTING.md#testing-and-ci)
for the Docker test workflow and [firmware build instructions](../README.md)
for MCU artifacts. Build outputs and caches are not source files to commit.
