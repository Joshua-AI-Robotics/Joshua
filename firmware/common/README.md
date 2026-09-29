# Shared firmware and JoshuaWire

Shared command definitions and codecs connect the [host board layer](../../robot/README.md)
to Joshua's [MCU firmware](../README.md). Board firmware also uses the command
dispatcher and STEP/DIR backend here. Start with `joshua_wire_commands.h`, then
the codec for the wire version you are working on.

The STEP/DIR backend can move real motors when compiled into firmware. See the
[hardware-safety rules](../../AGENTS.md#hardware-safety--read-this-first).

## Contents

- [joshua_wire_commands.h](joshua_wire_commands.h) and `.c` — version-neutral
  command views (`jw_command_t`), IDs, semantic types and payload-only codecs.
  `JW_CMD_*` describes an operation, not a wire version; RESET_SESSION is
  supported only by v2 endpoints. Payload codecs never add headers, IDs or CRCs.
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
- [joshua_wire_ethercat.h](joshua_wire_ethercat.h) — shared layout-v1 PDO/CoE
  sizes, offsets, object indices and transport bits. The host adapters use this
  contract for every board; this header itself implements no
  endpoint or watchdog.
- [joshua_ethercat_profile.h](joshua_ethercat_profile.h) / `.c` — board- and
  slave-stack-independent firmware endpoint: descriptor, CoE/PDO correlation,
  shared session, retained replies, fault latch and per-channel target watchdogs.
  Identity and command/reset/stop/enabled callbacks come from the board.
- [soes/](soes/README.md) — optional board-independent SOES binding of that
  profile: fixed dictionary, PDO mapping and compiler/stack options. Excluded
  from the ordinary PlatformIO source set; SOES is a separately licensed dependency.
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

The [AM243 overlay](../am243/joshua_dual_transport_v1/README.md#opt-in-jw2-ethercat-profile)
adapts this shared EtherCAT profile to its TI stack and software-only channel.
Physical-output safety and cross-transport arbitration remain unfinished. The
[separation plan](../../docs/BOARD_COMM_SEPARATION_PLAN.md) tracks that work.

## Porting JW2 EtherCAT to another board

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
5. Configure the host's slave index, paired JW2 transport and explicit timing
   budgets. Layout-v1 currently requires split LRD/LWR support; other layouts or
   capabilities require a versioned generic extension, never board-name checks.
6. Validate identity, reset, both command planes, duplicates, per-channel
   freshness, OP/link loss and stop behavior on the new hardware. Native tests
   cannot establish physical-output safety or production timing.

The opt-in AM243 SOES artifact replaces the slave-stack dependency at build time,
but has not yet passed hardware qualification. Existing TI artifacts retain
their evaluation limit. TI PRU firmware licensing, product identity and endurance
validation remain separate work; no fully open firmware claim is made here.

## Command and frame boundaries

Firmware handlers consume `jw_command_t` and return payload bytes. The serial
endpoint supplies the selected envelope; the v2 firmware session supplies IDs
and retry handling. Host channels likewise use neutral commands: the board's
command client selects the v1 codec or `JoshuaWireV2Session`, which exchanges
v2 frames directly through comm and returns validated payloads. There are no
intermediate v1 frames on either v2 path. `jw1_frame_t` remains appropriate in
the actual v1 codec/path, not as a version-neutral command representation.

## Remaining migration work

- Host message/cyclic board-engine composition and factory assembly now exist.
  Framed-serial extraction and configurable serial exchange/settle timing are
  implemented. V1 still uses the separate legacy fixed-size API and its existing
  timing; firmware wire formats are unchanged.
- **AM243 [command handler](../am243/joshua_dual_transport_v1/src/joshua_commands.h):**
  now transport-neutral and reused by separate UART and EtherCAT artifacts.
  Simultaneous UART/CoE/PDO ownership still needs an arbiter; physical motion
  needs a real motor backend. ROS 2 host support alone supplies neither.

## Tests and builds

For manual serial checks after an intentional flash, use the
[v2 validation guide](../../docs/JOSHUA_WIRE_V2_VALIDATION.md). The shared probe
defaults to reset/identify/ESTOP only; old board-specific smokes still use v1.

`*_test.cc` files are maintained source, kept beside the code they verify:
commands tests pin wire values, payload bytes and bounds, v1 tests cover legacy
framing, and v2 tests cover framing, firmware sessions and the serial endpoint. Board-specific native
tests also compile the real Teensy/ESP32 dispatch with the test-only
[Arduino substitute](../testing/Arduino.h) and
[shared test suite](../testing/serial_firmware_test.cc). Those helpers use no
physical I/O and do not validate pulse timing.

[BUILD](BUILD) defines host/native targets; [library.json](library.json) exposes
MCU sources to PlatformIO and excludes `*_test.cc`. AM243's Makefile lists its
C sources explicitly. See [CONTRIBUTING](../../CONTRIBUTING.md#testing-and-ci)
for the Docker test workflow and [firmware build instructions](../README.md)
for MCU artifacts. Build outputs and caches are not source files to commit.
