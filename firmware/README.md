# Joshua Firmware

This directory tracks firmware that Joshua expects on external boards.

Joshua can own two different kinds of firmware records:

- Joshua-owned firmware source and shared codecs that can be built from this
  repository.
- Metadata for vendor or bring-up firmware that Joshua can validate against,
  but should not vendor until redistribution rights are clear.

Runtime code must not flash boards automatically. Flashing is a board-management
operation that will eventually live behind `tools/flash/` and a firmware
manifest. Runtime board initialization should detect a mismatch and report the
firmware/artifact needed to fix it.

## How to flash a board

Every board's README under `firmware/<board>/` (or `firmware/<board>/<version>/`
for versioned boards) follows the same structure — Prerequisites, Install,
Build, Flash, Verify, Wiring/Pinout, Known gaps — defined in
[`FLASHING_TEMPLATE.md`](FLASHING_TEMPLATE.md). Start there for "how do I
flash board X"; where a board hasn't been brought up yet, its README says
so explicitly with `TODO` placeholders rather than staying silent.

## Current firmware records

JoshuaWire (JW) version `0.0.2` is the sole Joshua protocol. JW1 is removed;
former JW2 code, APIs and build targets now use the unversioned JW name.
The validated wire revision remains `2`, including CRC, session/message IDs,
command payloads and EtherCAT layout. Vendor TI-demo metadata remains separate.

| Board/path | Status | README |
| --- | --- | --- |
| AM243 UART | Serial protocol validated; software channel with no GPIO output | [AM243](am243/joshua_dual_transport/README.md) |
| AM243 JW EtherCAT, TI stack | Protocol and watchdog bench validated; software-only channel | [AM243 EtherCAT profile](am243/joshua_dual_transport/README.md#opt-in-jw-ethercat-profile) |
| Teensy 4.1 serial | Protocol validated; powered forward/return bench passed | [Teensy](teensy/41/README.md) |
| ESP32 serial | Protocol validated; powered motion remains untested | [ESP32](esp32/README.md) |
| Arduino | Not started | [Arduino](arduino/README.md) |
| AM243 vendor TI EtherCAT demo | Historical vendor bring-up metadata | [TI demo](am243/ti_ethercat_simple_demo_v1/README.md) |

## JoshuaWire serial

Build without flashing:

```bash
pio run -d firmware/teensy/41 -e teensy41-serial
pio run -d firmware/esp32 -e esp32-serial
firmware/am243/joshua_dual_transport/scripts/build.sh
```

Select `protocol: JOSHUA_WIRE` and `firmware { min_proto_version: 2 }` in the
host Board config. Omitted protocol selects JW for Joshua boards; vendor boards
retain their own protocol. There is no auto-detection or fallback to JW1.
Existing explicit `JOSHUA_WIRE_V2` configs must use `JOSHUA_WIRE`.

[Serial validation](../docs/JOSHUA_WIRE_VALIDATION.md#hardware-validation-status)
passed on AM243 UART, Teensy 4.1 and ESP32. Teensy also passed a powered
forward/return bench; ESP32 powered motion and the ROS 2 path remain untested.
The AM243 default image serves JW on UART; EtherCAT still runs TI's demo with
separate state and no physical motor output. Use the separate JW EtherCAT
artifact below for Joshua EtherCAT runtime.

JW requires RESET_SESSION before commands. Reset disables existing outputs
before clearing configuration; the host must configure and explicitly enable
channels again. ESTOP is latched until a new session. The serial firmware session
executes strictly increasing message IDs, replays the cached response for an
identical immediate retry, and drops older IDs or changed requests reusing an ID.
Session IDs provide correlation, not authentication. Serial has no communication-loss
watchdog; simultaneous transport ownership still requires an arbiter.

Native Bazel tests exercise the actual MCU dispatch with simulated serial/GPIO,
the AM243 software handler and host sessions. Tests never flash or move hardware.

## Opt-in AM243 JoshuaWire EtherCAT milestone

The [AM243 overlay](am243/joshua_dual_transport/README.md#opt-in-jw-ethercat-profile)
also builds the explicit `am243_ethercat_jw` artifact. It replaces TI's echo
profile with JW CoE management and 80-byte PDO command/feedback images, one
shared session, retained responses and latched software watchdogs. UART protocol
service is absent from this artifact; the UART/TI-demo image is built separately.

The endpoint and watchdog policy now live in `common/joshua_ethercat_profile`.
AM243 supplies identity, its TI-stack bridge and drive callbacks. Other boards
reuse that core and the same host communication implementation; see the
[firmware porting contract](common/README.md#porting-jw-ethercat-to-another-board).
The original AM243 images still link TI's one-hour evaluation stack. A separate
[SOES candidate](am243/joshua_dual_transport/README.md#opt-in-soes-replacement)
now builds without that stack and passes native protocol tests. It has been
flashed and passed discovery/handshake, but [register timeouts still block
hardware/endurance qualification](../docs/JOSHUA_WIRE_VALIDATION.md#soes-candidate-bring-up--2026-09-28),
so evaluation-stack retirement remains pending.

The production firmware core has native host-interoperability and controlled-clock
tests. A [single-board EtherCAT bench check](../docs/JOSHUA_WIRE_VALIDATION.md#recorded-am243-ethercat-result--2026-09-28)
passed factory/engine commands, software feedback and stale-target fault/recovery.
It also exposed a host mailbox deadline failure with a 1 ms budget; production
timing validation remains open. There is still no physical motor backend or
simultaneous transport arbitration.
Explicit watchdog intervals are required at build time; see the profile README
for the mapping, build command and safety limits. No flashing is automatic.

## Layout

For the protocol/session/dispatch file map and contributor entry points, see
[Shared firmware and JoshuaWire](common/README.md).

```text
firmware/
  FLASHING_TEMPLATE.md   the section structure every board README follows
  common/       # shared commands, JW codec, sessions and drive backend
  testing/      # native-test Arduino substitute and shared firmware tests
  am243/        # TI demo metadata plus Joshua's dual-transport source overlay
  teensy/41/    # Joshua-owned firmware for the Teensy 4.1
  arduino/      # not started — placeholder README only
  esp32/        # Joshua-owned firmware for ESP32, flashed & protocol-verified
```

Firmware variants should be explicit build artifacts. A board may support more
than one transport in a single image when the transports can coexist safely;
artifact names still identify the board, transport set, and protocol version.
