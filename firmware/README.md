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

## Current Firmware Records

| Board | Status | README |
| --- | --- | --- |
| AM243 (LP-AM243, TI EtherCAT demo) | Hello World + EtherCAT slave demo built, flashed, verified on real hardware. Vendor firmware — metadata only, stays as-is. | [`am243/ti_ethercat_simple_demo_v1/README.md`](am243/ti_ethercat_simple_demo_v1/README.md) |
| AM243 (LP-AM243, EtherCAT + `joshua_wire_v1`) | Built, flashed, and verified over both transports. Serial is a motion-safe software channel with no GPIO output. | [`am243/joshua_dual_transport_v1/README.md`](am243/joshua_dual_transport_v1/README.md) |
| Teensy 4.1 (STEP/DIR over `joshua_wire_v1`) | Built, flashed, verified end to end on real hardware, including physical motor rotation through the real production path. | [`teensy/41/README.md`](teensy/41/README.md) |
| Arduino (STEP/DIR over `joshua_wire_v1`) | Not started — real future board (`docs/BOARD_LAYER_RFC.md` §10 Phase 5), not retired by Teensy being first. | [`arduino/README.md`](arduino/README.md) |
| ESP32 (STEP/DIR over `joshua_wire_v1`) | Built, flashed, and protocol-verified on real hardware (IDENTIFY/ENABLE/SET_TARGET all confirmed) — joins the same joshua_wire_v1 family as Teensy. Physical motor rotation not yet observed on this board. | [`esp32/README.md`](esp32/README.md) |

- `common/joshua_wire_commands.{h,c}`: version-neutral command views, IDs
  (`JW_CMD_*`), semantic types (`jw_*_t`) and payload-only codecs. Both wire
  versions use the same payload serialization. Command IDs do not select a wire
  version;
  `JW_CMD_RESET_SESSION` still requires a v2 endpoint. V1 frame-building helpers
  keep their `jw1_*` names because they actually produce/consume v1 frames.
- `common/joshua_wire_v1.{h,c}`: the shared frame codec between Joshua host
  boards and Joshua-authored MCU firmware (docs/BOARD_LAYER_RFC.md §7.2/§7.3).
  Built as a Bazel `cc_library` for the host and as a PlatformIO library
  (`library.json`) for every firmware target — same two files, two
  toolchains, one repo commit.
- AM243 serial uses the shared `JoshuaWireBoard` engine. The dual-transport
  firmware overlay still includes TI echo EtherCAT alongside serial, keeping
  the TI SDK outside the repository, but the TI-demo host path is retired.
  Current Joshua EtherCAT runtime requires the separate JW2 artifact below.

## Opt-in JoshuaWire v2 serial milestone

The existing v1 artifacts remain the defaults. Separate v2 artifacts now use
the shared `common/joshua_wire_v2` codec and firmware session on Teensy 4.1,
ESP32, and AM243 UART. Build without flashing:

```bash
pio run -d firmware/teensy/41 -e teensy41-serial-v2
pio run -d firmware/esp32 -e esp32-serial-v2
JOSHUA_WIRE_VERSION=2 firmware/am243/joshua_dual_transport_v1/scripts/build.sh
```

Select `protocol: JOSHUA_WIRE_V2` in the host Board configuration.
Version selection is explicit; there is no auto-detection or fallback.
[Serial validation](../docs/JOSHUA_WIRE_V2_VALIDATION.md#hardware-validation-status)
passed on AM243 UART, Teensy 4.1 and ESP32. Teensy also passed a powered
forward/return bench; ESP32 powered motion and the v2 ROS 2 path remain untested.
The table above records historical v1 results.
The AM243 v2 artifact changes UART only; EtherCAT still runs TI's demo with
separate state, and UART still has no physical motor output.

V2 requires RESET_SESSION before commands. Reset disables existing outputs
before clearing configuration; the host must configure and explicitly enable
channels again. ESTOP is latched until a new session. The serial firmware session executes
strictly increasing message IDs, replays the cached response for an identical
immediate retry, and drops older IDs or changed requests reusing an ID. It is
single-threaded/single-flight, not a multi-transport command arbiter. Session IDs
provide correlation, not authentication: a different-session reset is accepted.
No communication-loss watchdog has been added in this milestone.

V1 wire encoding is unchanged. Shared command validation now rejects enabling
unconfigured channels and malformed/non-finite targets. Native Bazel tests
exercise the actual MCU dispatch with simulated serial/GPIO, plus the AM243
software handler and host session; no tests flash or move hardware.

## Opt-in AM243 JoshuaWire v2 EtherCAT milestone

The [AM243 overlay](am243/joshua_dual_transport_v1/README.md#opt-in-jw2-ethercat-profile)
also builds the explicit `am243_ethercat_jw2` artifact. It replaces TI's echo
profile with JW2 CoE management and 80-byte PDO command/feedback images, one
shared session, retained responses and latched software watchdogs. UART protocol
service is absent from this artifact; existing UART/TI-demo artifacts are unchanged.

The endpoint and watchdog policy now live in `common/joshua_ethercat_profile`.
AM243 supplies identity, its TI-stack bridge and drive callbacks. Other boards
reuse that core and the same host communication implementation; see the
[firmware porting contract](common/README.md#porting-jw2-ethercat-to-another-board).
The original AM243 images still link TI's one-hour evaluation stack. A separate
[SOES candidate](am243/joshua_dual_transport_v1/README.md#opt-in-soes-replacement)
now builds without that stack and passes native protocol tests. It has been
flashed and passed discovery/handshake, but [register timeouts still block
hardware/endurance qualification](../docs/JOSHUA_WIRE_V2_VALIDATION.md#soes-candidate-bring-up--2026-09-28),
so evaluation-stack retirement remains pending.

The production firmware core has native host-interoperability and controlled-clock
tests. A [single-board EtherCAT bench check](../docs/JOSHUA_WIRE_V2_VALIDATION.md#recorded-am243-ethercat-result--2026-09-28)
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
  common/       # shared commands, v1/v2 codecs, sessions and drive backend
  testing/      # native-test Arduino substitute and shared firmware tests
  am243/        # TI demo metadata plus Joshua's dual-transport source overlay
  teensy/41/    # Joshua-owned firmware for the Teensy 4.1
  arduino/      # not started — placeholder README only
  esp32/        # Joshua-owned firmware for ESP32, flashed & protocol-verified
```

Firmware variants should be explicit build artifacts. A board may support more
than one transport in a single image when the transports can coexist safely;
artifact names still identify the board, transport set, and protocol version.
